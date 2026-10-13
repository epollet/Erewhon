#include "World/Apothecary.h"

#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Items/ItemDatabase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Texture.h"
#include "ProceduralMeshComponent.h"
#include "World/Door.h"
#include "World/ErewhonSound.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorTypes.h"
#include "World/SpawnCabin.h"
#include "World/SpawnLayout.h"
#include "World/Terrain.h"

#define LOCTEXT_NAMESPACE "ErewhonApothecary"

using namespace ErewhonVisuals;

namespace
{
	/** Chaque potion et la plante dont l'écrasé lui donne son effet. */
	const TPair<const TCHAR*, const TCHAR*> GPotions[] = {
		{ TEXT("potion_speed"), TEXT("flower_blue") },
		{ TEXT("potion_night"), TEXT("flower_purple") },
		{ TEXT("potion_strength"), TEXT("weed_nettle") },
		{ TEXT("potion_jump"), TEXT("fern") },
		{ TEXT("potion_shrink"), TEXT("herb") },
		{ TEXT("potion_grow"), TEXT("weed_dock") },
	};

	FName Crushed(FName Plant) { return FName(*(TEXT("crushed_") + Plant.ToString())); }

	const FLinearColor GFireColor = FLinearColor::FromSRGBColor(FColor(255, 150, 60));
	const FLinearColor GBark = FLinearColor::FromSRGBColor(FColor(96, 70, 48));
}

// ── Plan et recettes ─────────────────────────────────────────────────────────

namespace ErewhonApothecary
{
	FVector MortarLocal() { return FVector(40.f, -45.f, ErewhonSpawn::CabinFloorTop + 82.f); }
	FVector CauldronLocal() { return HearthLocal() + FVector(0.f, 0.f, 46.f); }   // pendu à la crémaillère, au-dessus des flammes
	FVector HearthLocal() { return FVector(ErewhonSpawn::CabinHalfLength - ErewhonSpawn::CabinWallThickness - 42.f, 0.f, ErewhonSpawn::CabinFloorTop + 8.f); }

	FTransform ShelfLocal(int32 Shelf)
	{
		// Contre le mur +Y, de part et d'autre de la fenêtre (centrée, ±100 cm).
		const float X[ShelfCount] = { -320.f, -180.f, 230.f };
		return FTransform(FRotator(0.f, 90.f, 0.f),
			FVector(X[FMath::Clamp(Shelf, 0, ShelfCount - 1)], ErewhonSpawn::CabinHalfWidth - ErewhonSpawn::CabinWallThickness - 20.f, ErewhonSpawn::CabinFloorTop));
	}

	bool IsShelvable(const FItemDef& Def)
	{
		return Def.IsPotion() || Def.Id == TEXT("potion_base") || Def.Id.ToString().StartsWith(TEXT("crushed_"));
	}

	float ShelfRowZ(int32 Row)
	{
		return 8.f + 46.f * Row;   // le dessus des planches de l'étagère (voir ASpawnCabin::AddShelf)
	}

	int32 DoorSide(const FIntPoint& Sector)
	{
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (ErewhonSector::IsRoad(Sector + ErewhonSector::Sides[Side]))
			{
				return Side;
			}
		}
		// Loin des routes : la porte regarde vers le village.
		if (FMath::Abs(Sector.X) >= FMath::Abs(Sector.Y))
		{
			return Sector.X > 0 ? 1 : 0;
		}
		return Sector.Y > 0 ? 3 : 2;
	}

	FTransform HouseTransform(const FIntPoint& Sector, int32 WorldSeed)
	{
		// La porte (−X de la maison) regarde vers DoorSide ; la maison repose sur le point haut de son emprise.
		const FVector2D Toward(ErewhonSector::Sides[DoorSide(Sector)]);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Toward.Y, -Toward.X));
		const FVector2D Center(ErewhonSector::Center(Sector));
		const FRotator Rotation(0.f, Yaw, 0.f);
		float Top = ErewhonTerrain::Height(Center, WorldSeed);
		for (const float SX : { -1.f, 1.f })
		{
			for (const float SY : { -1.f, 1.f })
			{
				const FVector Corner = Rotation.RotateVector(FVector(SX * (ErewhonSpawn::CabinHalfLength + 55.f), SY * (ErewhonSpawn::CabinHalfWidth + 55.f), 0.f));
				Top = FMath::Max(Top, ErewhonTerrain::Height(Center + FVector2D(Corner), WorldSeed));
			}
		}
		return FTransform(Rotation, FVector(Center, Top));
	}

	bool HasHouse(const AErewhonGameState* State, const FIntPoint& Sector)
	{
		return State && (State->GetBuildings(Sector) & ESectorBuilding::Apothecary) != 0;
	}

	bool IsInside(const UObject* WorldContext, const FVector& World)
	{
		const AErewhonGameState* State = AErewhonGameState::Get(WorldContext);
		const FIntPoint Sector = ErewhonSector::FromWorld(World);
		if (!HasHouse(State, Sector))
		{
			return false;
		}
		const FVector Local = HouseTransform(Sector, State->GetWorldSeed()).InverseTransformPosition(World);
		return FMath::Abs(Local.X) < ErewhonSpawn::CabinHalfLength && FMath::Abs(Local.Y) < ErewhonSpawn::CabinHalfWidth
			&& Local.Z > ErewhonSpawn::CabinFloorTop - 20.f;
	}

	TArray<FRecipe> Recipes(const UItemDatabase& Items, bool bCauldron)
	{
		TArray<FRecipe> Out;
		if (!bCauldron)
		{
			// Le mortier : chaque plante donne son écrasé.
			for (const FItemDef* Plant : Items.GetPlants())
			{
				if (Items.Find(Crushed(Plant->Id)))
				{
					Out.Add({ Crushed(Plant->Id), { { Plant->Id, 1 } }, false });
				}
			}
			return Out;
		}
		// Le chaudron : deux écrasés d'herbe font une base ; une base et deux écrasés d'une plante, une potion.
		const FName Base(TEXT("potion_base"));
		const FName Grass = Crushed(TEXT("grass"));
		if (Items.Find(Base) && Items.Find(Grass))
		{
			Out.Add({ Base, { { Grass, 2 } }, true });
		}
		for (const TPair<const TCHAR*, const TCHAR*>& Potion : GPotions)
		{
			const FName Ingredient = Crushed(Potion.Value);
			if (Items.Find(Potion.Key) && Items.Find(Ingredient))
			{
				Out.Add({ FName(Potion.Key), { { Base, 1 }, { Ingredient, 2 } }, true });
			}
		}
		return Out;
	}

	TOptional<FRecipe> FindRecipe(const UItemDatabase& Items, FName Output)
	{
		for (const bool bCauldron : { false, true })
		{
			for (const FRecipe& Recipe : Recipes(Items, bCauldron))
			{
				if (Recipe.Output == Output)
				{
					return Recipe;
				}
			}
		}
		return {};
	}
}

// ── La maison ────────────────────────────────────────────────────────────────

AApothecary::AApothecary()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;   // une maison ne disparaît pas quand on s'éloigne
	SetNetUpdateFrequency(2.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AApothecary::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AApothecary, Sector);
	DOREPLIFETIME(AApothecary, FireUntil);
	DOREPLIFETIME(AApothecary, Shelves);
}

void AApothecary::Setup(const FIntPoint& InSector, double InFireUntil, const TArray<FInventorySlot>& InShelves)
{
	Sector = InSector;
	FireUntil = InFireUntil;
	Shelves = InShelves;
	Shelves.SetNum(ErewhonApothecary::ShelfSlots);
}

void AApothecary::CommitShelves()
{
	ForceNetUpdate();
	OnRep_Shelves();   // l'hôte d'une partie en écoute ne reçoit pas d'OnRep
}

void AApothecary::OnRep_Shelves()
{
	BuildShelfPotions();
	OnShelvesChanged.Broadcast();
}

void AApothecary::BuildShelfPotions()
{
	if (!bBuilt || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	for (UStaticMeshComponent* Bottle : ShelfBottles)
	{
		if (Bottle)
		{
			Bottle->DestroyComponent();
		}
	}
	ShelfBottles.Reset();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FLinearColor Cork = FLinearColor::FromSRGBColor(FColor(150, 110, 70));
	UStaticMesh* Bowl = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bowl"));
	for (int32 Index = 0; Index < Shelves.Num() && Index < ErewhonApothecary::ShelfSlots; ++Index)
	{
		const FInventorySlot& Slot = Shelves[Index];
		const FItemDef* Def = Items && !Slot.IsEmpty() ? Items->Find(Slot.ItemId) : nullptr;
		if (!Def)
		{
			continue;
		}
		// Une pile par rayon : un flacon (ou un bol) par exemplaire, alignés.
		const bool bCrushed = Def->Id.ToString().StartsWith(TEXT("crushed_"));
		UStaticMesh* Model = bCrushed ? Bowl : Cast<UStaticMesh>(Def->Mesh.TryLoad());
		const FTransform Shelf = ErewhonApothecary::ShelfLocal(Index / ErewhonApothecary::ShelfRows);
		const float Z = ErewhonApothecary::ShelfRowZ(Index % ErewhonApothecary::ShelfRows);
		for (int32 k = 0; k < FMath::Min(Slot.Count, ErewhonApothecary::PotionsPerSlot); ++k)
		{
			const FVector Foot = Shelf.TransformPosition(FVector(-4.f, -45.f + k * 10.f, Z));
			if (Model)
			{
				ShelfBottles.Add(AddGeneratedMesh(this, Root, Model, Foot, bCrushed ? 4.5f : 14.f, Shelf.Rotator().Yaw + 180.f + (k % 3 - 1) * 12.f, false));
				if (bCrushed)
				{
					// La pâte de plante dans le bol, à sa couleur.
					ShelfBottles.Add(AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, 3.6f), FVector(6.5f, 6.5f, 1.2f), Def->Color, FRotator::ZeroRotator, false));
				}
				continue;
			}
			UStaticMeshComponent* Body = AddShape(this, Root, EShape::Sphere, Foot + FVector(0, 0, 5.5f), FVector(8.f, 8.f, 11.f), Def->Color, FRotator::ZeroRotator, false);
			UStaticMeshComponent* Stopper = AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, 13.f), FVector(3.5f, 3.5f, 4.f), Cork, FRotator::ZeroRotator, false);
			ShelfBottles.Add(Body);
			ShelfBottles.Add(Stopper);
		}
	}
	for (UStaticMeshComponent* Part : ShelfBottles)
	{
		if (Part)
		{
			Part->SetCastShadow(false);
		}
	}
}

bool AApothecary::IsFireLit() const
{
	return FireRemaining() > 0.0;
}

double AApothecary::FireRemaining() const
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	return State ? FMath::Max(0.0, FireUntil - State->GetWorldClock()) : 0.0;
}

void AApothecary::AddFire(double Seconds)
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State)
	{
		return;
	}
	const double Now = State->GetWorldClock();
	FireUntil = FMath::Min(FMath::Max(FireUntil, Now) + Seconds, Now + ErewhonApothecary::MaxFireSeconds);
	ForceNetUpdate();
	OnFireChanged.Broadcast();
}

void AApothecary::BeginPlay()
{
	Super::BeginPlay();
	BuildLocal();
}

void AApothecary::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Revente du secteur : la maison et ses postes disparaissent avec elle.
	if (House)
	{
		House->Destroy();
	}
	for (AActor* Station : Stations)
	{
		if (Station)
		{
			Station->Destroy();
		}
	}
	if (Door && HasAuthority())
	{
		Door->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

void AApothecary::BuildLocal()
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (bBuilt || !State)
	{
		return;
	}
	bBuilt = true;
	const FTransform HouseT = ErewhonApothecary::HouseTransform(Sector, State->GetWorldSeed());
	SetActorTransform(HouseT);

	// La maison elle-même : une cabane du village, à l'habillage de l'apothicaire.
	House = GetWorld()->SpawnActorDeferred<ASpawnCabin>(ASpawnCabin::StaticClass(), HouseT, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	House->Kind = EShopKind::Apothecary;
	House->FinishSpawning(HouseT);

	// La porte (répliquée) : le serveur la pose.
	if (HasAuthority())
	{
		const FTransform Hinge = FTransform(FVector(-ErewhonSpawn::CabinHalfLength + ErewhonSpawn::CabinWallThickness * 0.5f,
			-ErewhonSpawn::DoorHalfWidth, ErewhonSpawn::CabinFloorTop)) * HouseT;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Door = GetWorld()->SpawnActor<ADoor>(Hinge.GetLocation(), Hinge.Rotator(), Params);
	}

	// Les postes : le mortier et le chaudron sur la table, la cheminée.
	struct FSpot { EApothecaryStation Kind; FTransform Local; int32 Shelf; };
	const FSpot Spots[] = {
		{ EApothecaryStation::Mortar, FTransform(ErewhonApothecary::MortarLocal()), 0 },
		{ EApothecaryStation::Cauldron, FTransform(ErewhonApothecary::CauldronLocal()), 0 },
		{ EApothecaryStation::Shelf, ErewhonApothecary::ShelfLocal(0), 0 },
		{ EApothecaryStation::Shelf, ErewhonApothecary::ShelfLocal(1), 1 },
		{ EApothecaryStation::Shelf, ErewhonApothecary::ShelfLocal(2), 2 },
	};
	for (const FSpot& Spot : Spots)
	{
		const FTransform At = Spot.Local * HouseT;
		AApothecaryStation* Station = GetWorld()->SpawnActorDeferred<AApothecaryStation>(AApothecaryStation::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Station->Setup(this, Spot.Kind, Spot.Shelf);
		Station->FinishSpawning(At);
		Stations.Add(Station);
	}

	// Quelques grands arbres autour, sur les diagonales : jamais sur un chemin ni devant la porte.
	FRandomStream Rand(HashCombine(GetTypeHash(State->GetWorldSeed()), GetTypeHash(Sector)) ^ 0x51A7);
	TArray<UStaticMesh*> TreeModels;
	for (const TCHAR* Name : { TEXT("SM_tree_linden"), TEXT("SM_tree_oak") })
	{
		if (UStaticMesh* Model = LoadGenerated<UStaticMesh>(TEXT("Meshes"), Name))
		{
			TreeModels.Add(Model);
		}
	}
	const FVector2D Center(ErewhonSector::Center(Sector));
	for (int32 i = 0; i < 4 && TreeModels.Num() > 0; ++i)
	{
		const float Angle = FMath::DegreesToRadians(45.f + 90.f * i + Rand.FRandRange(-14.f, 14.f));
		const FVector2D Foot2D = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Rand.FRandRange(1250.f, 1500.f);
		const FVector Foot = HouseT.InverseTransformPosition(FVector(Foot2D, ErewhonTerrain::Height(Foot2D, State->GetWorldSeed()) - 10.f));
		AddGeneratedMesh(this, Root, TreeModels[i % TreeModels.Num()], Foot, Rand.FRandRange(900.f, 1150.f), Rand.FRandRange(0.f, 360.f), false);
		UStaticMeshComponent* Trunk = AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, 150), FVector(60, 60, 300), GBark, FRotator::ZeroRotator, true);
		Trunk->SetHiddenInGame(true);
	}

	// Le foyer : des bûches, toujours là ; flammes, lueur et crépitement quand le feu brûle.
	const FVector Hearth = ErewhonApothecary::HearthLocal();
	if (UStaticMesh* Logs = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_firewood")))
	{
		AddGeneratedMesh(this, Root, Logs, Hearth, 26.f, 90.f, false);
	}
	else
	{
		for (const float Yaw : { 20.f, -25.f, 80.f })
		{
			AddShape(this, Root, EShape::Cylinder, Hearth + FVector(0, 0, 8), FVector(11, 11, 60), GBark, FRotator(90.f, Yaw, 0.f), false);
		}
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	BuildShelfPotions();
	// Les flammes : des flammes peintes croisées, que M_Flame fait onduler, autour et sous le chaudron.
	UMaterialInterface* FlameMaterial = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Flame"));
	UTexture* FlameTexture = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_flame"));
	if (FlameMaterial && FlameTexture)
	{
		struct FTongue { FVector Offset; float Yaw; float Width; float Height; };
		const FTongue Tongues[] = {
			{ FVector(-6, 0, 12), 0.f, 50.f, 50.f }, { FVector(-2, 0, 10), 60.f, 44.f, 46.f }, { FVector(-2, 0, 10), 120.f, 44.f, 48.f },
			{ FVector(6, -24, 0), 20.f, 34.f, 48.f }, { FVector(0, 24, 0), 160.f, 34.f, 46.f }, { FVector(14, 4, 0), 90.f, 30.f, 44.f },
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Tongues); ++i)
		{
			USceneComponent* Pivot = NewObject<USceneComponent>(this);
			Pivot->SetupAttachment(Root);
			Pivot->SetRelativeLocation(Hearth + Tongues[i].Offset + FVector(0, 0, 2));
			Pivot->RegisterComponent();
			AddInstanceComponent(Pivot);
			if (UProceduralMeshComponent* Tongue = AddPicture(this, Pivot, FVector(0, 0, Tongues[i].Height * 0.5f), Tongues[i].Width, Tongues[i].Height, Tongues[i].Yaw, FlameTexture))
			{
				UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(FlameMaterial, this);
				Glow->SetScalarParameterValue(TEXT("Seed"), i * 0.37f);
				Tongue->SetMaterial(0, Glow);
				Tongue->SetCastShadow(false);
				FlameGlows.Add(Glow);
			}
			Pivot->SetVisibility(false, true);
			FlamePivots.Add(Pivot);
		}
	}
	FireLight = NewObject<UPointLightComponent>(this);
	FireLight->SetupAttachment(Root);
	FireLight->SetRelativeLocation(Hearth + FVector(-60.f, 0.f, 30.f));
	FireLight->SetIntensityUnits(ELightUnits::Lumens);
	FireLight->SetIntensity(0.f);
	FireLight->SetAttenuationRadius(900.f);
	FireLight->SetLightColor(GFireColor);
	FireLight->SetSourceRadius(20.f);
	FireLight->SetVisibility(false);
	FireLight->RegisterComponent();
	AddInstanceComponent(FireLight);

	if (USoundWave* Crackle = ErewhonSound::Load(TEXT("amb_fire")))
	{
		FireSound = NewObject<UAudioComponent>(this);
		FireSound->SetupAttachment(Root);
		FireSound->SetRelativeLocation(Hearth + FVector(0, 0, 30));
		FireSound->SetSound(Crackle);
		FireSound->AttenuationSettings = ErewhonSound::Attenuation(150.f, 1400.f);
		FireSound->SetVolumeMultiplier(0.f);
		FireSound->bAutoActivate = false;
		FireSound->RegisterComponent();
		AddInstanceComponent(FireSound);
	}
}

void AApothecary::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		BuildLocal();   // client : l'état du jeu est arrivé après la maison
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateFire(DeltaSeconds);
	}
}

void AApothecary::UpdateFire(float DeltaSeconds)
{
	// Le feu prend et s'éteint en douceur ; la flamme vacille, la lumière avec elle.
	FireTime += DeltaSeconds;
	const float Target = IsFireLit() ? 1.f : 0.f;
	FireLevel = FMath::FInterpConstantTo(FireLevel, Target, DeltaSeconds, Target > FireLevel ? 0.6f : 0.25f);
	const bool bLit = FireLevel > 0.01f;
	const float Flicker = FMath::Clamp(1.f + 0.18f * FMath::PerlinNoise1D(FireTime * 2.3f) + 0.1f * FMath::PerlinNoise1D(FireTime * 11.f + 7.f), 0.6f, 1.4f);
	if (FireLight)
	{
		if (FireLight->IsVisible() != bLit)
		{
			FireLight->SetVisibility(bLit);
		}
		FireLight->SetIntensity(450.f * FireLevel * Flicker);
	}
	for (int32 i = 0; i < FlamePivots.Num(); ++i)
	{
		USceneComponent* Pivot = FlamePivots[i];
		if (Pivot->IsVisible() != bLit)
		{
			Pivot->SetVisibility(bLit, true);
		}
		if (bLit)
		{
			// Chaque flamme s'étire et retombe à son rythme ; le feu qui prend les fait grandir.
			const float Lick = 0.8f + 0.3f * FMath::PerlinNoise1D(FireTime * (3.f + i * 0.7f) + i * 13.f);
			const float Grow = FMath::Sqrt(FireLevel);
			Pivot->SetRelativeScale3D(FVector((0.9f + 0.1f * Lick) * Grow, (0.9f + 0.1f * Lick) * Grow, Lick * Grow));
		}
	}
	for (UMaterialInstanceDynamic* Glow : FlameGlows)
	{
		Glow->SetScalarParameterValue(TEXT("Intensity"), 14.f * Flicker);
	}
	if (FireSound)
	{
		if (bLit && !FireSound->IsPlaying())
		{
			FireSound->Play(FMath::FRandRange(0.f, 5.f));
		}
		else if (!bLit && FireSound->IsPlaying())
		{
			FireSound->Stop();
		}
		FireSound->SetVolumeMultiplier(0.8f * FireLevel * ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Effects));
	}
}

// ── Les postes ───────────────────────────────────────────────────────────────

AApothecaryStation::AApothecaryStation()
{
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Bounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	Bounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	RootComponent = Bounds;
}

void AApothecaryStation::Setup(AApothecary* InHouse, EApothecaryStation InKind, int32 InShelf)
{
	House = InHouse;
	Kind = InKind;
	ShelfIndex = InShelf;
}

void AApothecaryStation::BeginPlay()
{
	Super::BeginPlay();
	// Une zone de visée au-dessus du poste, et son objet posé dessus.
	USceneComponent* Base = NewObject<USceneComponent>(this);
	Base->SetupAttachment(Bounds);
	Base->RegisterComponent();
	switch (Kind)
	{
	case EApothecaryStation::Mortar:
		Bounds->SetBoxExtent(FVector(22.f, 22.f, 16.f));
		Base->SetRelativeLocation(FVector(0, 0, -16.f));
		if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_mortar")))
		{
			AddGeneratedMesh(this, Base, Mesh, FVector::ZeroVector, 20.f, 0.f, false);
		}
		else
		{
			AddShape(this, Base, EShape::Cylinder, FVector(0, 0, 7), FVector(24, 24, 14), Palette::Stone, FRotator::ZeroRotator, false);
			AddShape(this, Base, EShape::Cylinder, FVector(3, 0, 18), FVector(4, 4, 22), Palette::Stone, FRotator(20.f, 0.f, 0.f), false);
		}
		break;
	case EApothecaryStation::Cauldron:
		Bounds->SetBoxExtent(FVector(30.f, 30.f, 26.f));
		Base->SetRelativeLocation(FVector(0, 0, -26.f));
		if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cauldron")))
		{
			AddGeneratedMesh(this, Base, Mesh, FVector::ZeroVector, 48.f, 0.f, false);
		}
		else
		{
			AddShape(this, Base, EShape::Sphere, FVector(0, 0, 16), FVector(36, 36, 30), FLinearColor::FromSRGBColor(FColor(40, 40, 44)), FRotator::ZeroRotator, false);
			AddShape(this, Base, EShape::Cylinder, FVector(0, 0, 28), FVector(26, 26, 2), FLinearColor::FromSRGBColor(FColor(90, 170, 80)), FRotator::ZeroRotator, false);
		}
		break;
	case EApothecaryStation::Shelf:
		// Toute l'étagère se vise.
		Bounds->SetBoxExtent(FVector(20.f, 62.f, 96.f));
		Base->SetRelativeLocation(FVector(0, 0, -96.f));
		SetActorLocation(GetActorLocation() + FVector(0, 0, 96.f));
		break;
	default:
		Bounds->SetBoxExtent(FVector(40.f, 70.f, 55.f));
		Base->SetRelativeLocation(FVector(0, 0, -55.f));
		SetActorLocation(GetActorLocation() + FVector(0, 0, 55.f));
		break;
	}
	if (Kind == EApothecaryStation::Mortar || Kind == EApothecaryStation::Cauldron)
	{
		SetActorLocation(GetActorLocation() + FVector(0, 0, Bounds->GetUnscaledBoxExtent().Z));
	}
}

bool AApothecaryStation::IsLocalOwner() const
{
	const AApothecary* Home = House.Get();
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const AErewhonPlayerState* PS = PC ? PC->GetPlayerState<AErewhonPlayerState>() : nullptr;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const FString* Key = Home && State ? State->FindSectorOwnerKey(Home->GetSector()) : nullptr;
	return Key && PS && *Key == PS->GetPlayerKey();
}

FText AApothecaryStation::GetInteractionLabel() const
{
	const AApothecary* Home = House.Get();
	if (!Home)
	{
		return FText::GetEmpty();
	}
	if (!IsLocalOwner())
	{
		const AErewhonGameState* State = AErewhonGameState::Get(this);
		return FText::Format(LOCTEXT("OthersHouse", "Apothicairerie de {0}"), FText::FromString(State ? State->GetSectorOwnerName(Home->GetSector()) : FString()));
	}
	switch (Kind)
	{
	case EApothecaryStation::Mortar:
		return LOCTEXT("UseMortar", "Piler des plantes au mortier");
	case EApothecaryStation::Cauldron:
		return Home->IsFireLit() ? LOCTEXT("UseCauldron", "Préparer une potion au chaudron")
			: LOCTEXT("UseCauldronCold", "Le chaudron (le feu est éteint)");
	case EApothecaryStation::Shelf:
		return FText::Format(LOCTEXT("UseShelf", "Étagère {0} (potions et écrasés)"), FText::AsNumber(ShelfIndex + 1));
	default:
		if (Home->IsFireLit())
		{
			return FText::Format(LOCTEXT("FeedFire", "Remettre du petit bois (le feu brûle encore {0} min)"),
				FText::AsNumber(FMath::CeilToInt32(Home->FireRemaining() / 60.0)));
		}
		return LOCTEXT("LightFire", "Allumer le feu (petit bois)");
	}
}

void AApothecaryStation::Interact(AErewhonPlayerController* Controller)
{
	AApothecary* Home = House.Get();
	if (!Controller || !Home)
	{
		return;
	}
	if (!IsLocalOwner())
	{
		Controller->ShowToast(LOCTEXT("NotYourHouse", "Ce n'est pas votre apothicairerie."));
		return;
	}
	if (Kind == EApothecaryStation::Shelf)
	{
		Controller->OpenShelves(Home, ShelfIndex);
		return;
	}
	Controller->OpenApothecary(Home, Kind);
}

#undef LOCTEXT_NAMESPACE
