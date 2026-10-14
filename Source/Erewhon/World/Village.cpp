#include "World/Village.h"
#include "World/Storage.h"
#include "EngineUtils.h"
#include "Sound/SoundWave.h"
#include "World/ErewhonSound.h"
#include "Components/AudioComponent.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/ErewhonCharacter.h"
#include "World/ErewhonLights.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorLayout.h"
#include "World/Signpost.h"
#include "World/SpawnLayout.h"
#include "World/Terrain.h"

#define LOCTEXT_NAMESPACE "ErewhonVillage"

using namespace ErewhonVisuals;

namespace ErewhonVillage
{
	namespace
	{
		const FLinearColor GIron = FLinearColor::FromSRGBColor(FColor(52, 46, 40));
		const FLinearColor GLanternGlow = FLinearColor::FromSRGBColor(FColor(255, 196, 110));
		const FLinearColor GTerracotta = FLinearColor::FromSRGBColor(FColor(186, 102, 64));
		const FLinearColor GBark = FLinearColor::FromSRGBColor(FColor(104, 74, 52));

		/** Les objets de la place, posés en couronne autour de son centre (degrés, nord = 0). */
		constexpr float GBenchAngles[] = { 45.f, 135.f, 225.f, 315.f };
		constexpr float GLampAngles[] = { 22.5f, 112.5f, 202.5f, 292.5f };
		constexpr float GBushAngles[] = { 67.5f, 157.5f, 247.5f, 337.5f };
		/** De grands arbres autour de la place, qui l'ombragent ; ils évitent les chemins et les cabanes. */
		constexpr float GTreeAngles[] = { 20.f, 40.f, 115.f, 140.f, 160.f, 200.f, 295.f, 320.f, 340.f };

		FVector Plaza() { return ErewhonSpawn::CabinTransform(EShopKind::GeneralStore).GetLocation(); }

		/** Sur le modèle de la fontaine (repère de la fontaine) : le bec, et l'endroit où le tonneau reçoit l'eau (mesurés). */
		constexpr float FountainSide = -18.f;   // le pilier est un peu décentré par rapport au bassin
		const FVector FountainSpoutLocal(32.f, FountainSide, 132.f);
		const FVector FountainSpotLocal(62.f, FountainSide, 0.f);
		constexpr float FountainHeight = 190.f;
		constexpr float FountainMeshYaw = -90.f;   // la façade du modèle (+Y) vers +X

		FVector OnRing(float AngleDegrees, float Radius)
		{
			const float A = FMath::DegreesToRadians(AngleDegrees);
			return FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, 0.f);
		}

		bool HasUpgrade(const UObject* Context, EVillageUpgrade Upgrade)
		{
			const AErewhonGameState* State = AErewhonGameState::Get(Context);
			return State && State->HasVillageUpgrade(Upgrade);
		}
	}

	const TArray<FUpgradeInfo>& Upgrades()
	{
		static const TArray<FUpgradeInfo> List = {
			{ EVillageUpgrade::Tailor, LOCTEXT("Tailor", "Boutique du tailleur"),
				LOCTEXT("TailorDesc", "Une couturière s'installe au bord de la route de l'ouest : vêtements et poches en plus."), 300 },
			{ EVillageUpgrade::Signposts, LOCTEXT("Signposts", "Panneaux de direction"),
				LOCTEXT("SignpostsDesc", "Les points cardinaux à la sortie de la place, les boutiques indiquées à chaque carrefour."), 75 },
			{ EVillageUpgrade::Lanterns, LOCTEXT("Lanterns", "Lanternes"),
				LOCTEXT("LanternsDesc", "Sur la devanture des boutiques, autour de la place et aux carrefours du village. Elles s'allument la nuit."), 200 },
			{ EVillageUpgrade::Decoration, LOCTEXT("Decoration", "Embellissement de la place"),
				LOCTEXT("DecorationDesc", "Bordures de pierre le long des chemins, buissons et pots de fleurs, arbres, bancs où s'asseoir."), 200 },
			{ EVillageUpgrade::PavedCenter, LOCTEXT("PavedCenter", "Pavés du centre"),
				LOCTEXT("PavedCenterDesc", "La place et ses chemins pavés de pierre. On y marche 20 % plus vite."), 200 },
			{ EVillageUpgrade::PavedVillage, LOCTEXT("PavedVillage", "Pavés du village"),
				LOCTEXT("PavedVillageDesc", "Les routes du village pavées à leur tour. Les propriétaires pourront ensuite paver leurs chemins d'accès."), 400,
				EVillageUpgrade::PavedCenter },
		};
		return List;
	}

	const FUpgradeInfo& Info(EVillageUpgrade Upgrade)
	{
		return Upgrades()[FMath::Clamp(static_cast<int32>(Upgrade), 0, Upgrades().Num() - 1)];
	}

	bool IsInVillage(const FIntPoint& Sector)
	{
		const int32 Radius = UErewhonSettings::Get().StoreSectorRadius;
		return FMath::Max(FMath::Abs(Sector.X), FMath::Abs(Sector.Y)) <= Radius;
	}

	FTransform FountainTransform()
	{
		// Dans le carré d'herbe au-delà des arbres de la place, entre l'allée nord (+Y) et celle de l'ouest (−X),
		// le bec (+X) tourné vers l'allée nord, qu'un petit chemin rejoint.
		const FVector At = Plaza() + FVector(-850.f, 2050.f, 0.f);
		return FTransform(FRotator::ZeroRotator, FVector(At.X, At.Y, 0.f));
	}

	FVector FountainBarrelSpot()
	{
		return FountainTransform().TransformPosition(FountainSpotLocal);
	}

	const TArray<FTransform>& BenchSeats()
	{
		// Quatre bancs entre les chemins, tournés vers la place.
		static const TArray<FTransform> Seats = []()
		{
			TArray<FTransform> Result;
			for (const float Angle : GBenchAngles)
			{
				const FVector Location = Plaza() + OnRing(Angle, 1250.f) + FVector(0.f, 0.f, 45.f);
				Result.Add(FTransform(FRotator(0.f, Angle + 180.f, 0.f), Location));
			}
			return Result;
		}();
		return Seats;
	}

	void AddLantern(AActor* Owner, USceneComponent* Parent, const FVector& Location, float Yaw, bool bWall)
	{
		USceneComponent* Frame = NewObject<USceneComponent>(Owner);
		Frame->SetupAttachment(Parent);
		Frame->SetRelativeLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));
		Frame->RegisterComponent();
		Owner->AddInstanceComponent(Frame);

		const auto Iron = [Owner, Frame](EShape Shape, const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator, bool bCollision = false)
		{
			UStaticMeshComponent* Part = AddShape(Owner, Frame, Shape, Center, Size, GIron, Rotation, bCollision);
			Part->SetMaterial(0, MakePaintedMaterial(Part, TEXT("wood_trim"), 80.f, GIron, FLinearColor(0.45f, 0.42f, 0.4f)));
			return Part;
		};

		// Le crochet : un poteau et sa potence, ou une simple potence fixée au mur.
		FVector Hook;
		if (bWall)
		{
			Iron(EShape::Cube, FVector(2, 0, 0), FVector(4, 10, 16));
			Iron(EShape::Cube, FVector(16, 0, 6), FVector(28, 3, 3));
			Hook = FVector(28, 0, 4);
		}
		else
		{
			Iron(EShape::Cube, FVector(0, 0, 130), FVector(10, 10, 260), FRotator::ZeroRotator, true);
			Iron(EShape::Cone, FVector(0, 0, 260), FVector(16, 16, 10));
			Iron(EShape::Cube, FVector(24, 0, 250), FVector(50, 4, 4));
			Hook = FVector(44, 0, 248);
		}

		// La lanterne : un toit, une cage, un verre qui luit, et une petite lumière chaude.
		const FVector Body = Hook - FVector(0, 0, 20);
		Iron(EShape::Cone, Body + FVector(0, 0, 13), FVector(22, 22, 9));
		Iron(EShape::Cube, Body + FVector(0, 0, -13), FVector(18, 18, 3));
		for (const FVector2D Corner : { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(-1, -1) })
		{
			Iron(EShape::Cube, Body + FVector(Corner.X * 7.5f, Corner.Y * 7.5f, 0), FVector(2, 2, 26));
		}
		UStaticMeshComponent* Glass = AddShape(Owner, Frame, EShape::Cube, Body, FVector(13, 13, 22), GLanternGlow, FRotator::ZeroRotator, false);
		Glass->SetCastShadow(false);
		UErewhonLights* Lights = Owner->GetWorld()->GetSubsystem<UErewhonLights>();
		const float FlamePhase = FMath::FRandRange(0.f, 1000.f);
		if (UMaterialInterface* Emissive = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Emissive")))
		{
			UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(Emissive, Glass);
			Glow->SetVectorParameterValue(TEXT("Color"), GLanternGlow);
			Glass->SetMaterial(0, Glow);
			Lights->RegisterGlow(Glow, 4.f, FlamePhase);
		}

		UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
		Light->SetupAttachment(Frame);
		Light->SetRelativeLocation(Body);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetAttenuationRadius(bWall ? 800.f : 1200.f);
		Light->SetLightColor(GLanternGlow);
		Light->SetSourceRadius(6.f);
		Light->SetCastShadows(false);   // des dizaines de lanternes : pas d'ombres, pour les performances
		Light->RegisterComponent();
		Owner->AddInstanceComponent(Light);
		Lights->RegisterLight(Light, bWall ? 250.f : 500.f, FlamePhase);   // en applique, la façade est tout près : plus doux
	}
}

// ── Habillage de la place ────────────────────────────────────────────────────

AVillageDecor::AVillageDecor()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AVillageDecor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Les bancs sont des acteurs à part : ils partent avec l'habillage.
	TArray<AActor*> Attached;
	GetAttachedActors(Attached);
	for (AActor* Child : Attached)
	{
		Child->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

void AVillageDecor::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(ErewhonVillage::Plaza());
	const bool bTailor = ErewhonVillage::HasUpgrade(this, EVillageUpgrade::Tailor);
	const bool bSignposts = ErewhonVillage::HasUpgrade(this, EVillageUpgrade::Signposts);
	const bool bLanterns = ErewhonVillage::HasUpgrade(this, EVillageUpgrade::Lanterns);
	if (bSignposts)
	{
		BuildSignposts(bTailor, bLanterns);
	}
	if (bLanterns)
	{
		BuildLanterns(bTailor, bSignposts);
	}
	if (ErewhonVillage::HasUpgrade(this, EVillageUpgrade::Decoration))
	{
		BuildDecoration();
	}
}

namespace
{
	struct FExit { FVector2D Dir; const TCHAR* Name; };
	const FExit GExits[] = {
		{ FVector2D(1, 0), TEXT("North") }, { FVector2D(0, 1), TEXT("East") },
		{ FVector2D(-1, 0), TEXT("South") }, { FVector2D(0, -1), TEXT("West") },
	};

	/** Pied d'un poste à la sortie de la place : sur le bord droit (Side = 1) ou gauche (−1) du chemin. */
	FVector2D ExitFoot(const FExit& Exit, float Side)
	{
		const float Plaza = UErewhonSettings::Get().PlazaRadiusMeters * 100.f;
		const FVector2D Right(-Exit.Dir.Y, Exit.Dir.X);
		return Exit.Dir * (Plaza + 80.f) + Right * Side * (ErewhonSpawn::PathWidth * 0.5f + 60.f);
	}

	/** Pied d'un poste au carrefour d'une cabane du bord des routes (Side = 1 : côté place). */
	FVector JunctionFoot(const AActor* Decor, EShopKind Shop, float Side)
	{
		const FVector Doorstep = Decor->GetActorTransform().InverseTransformPosition(ErewhonSpawn::DoorstepLocation(Shop));
		const float SideX = FMath::Sign(Doorstep.X);
		const float TowardPlaza = -FMath::Sign(Doorstep.Y);
		return FVector(SideX * (ErewhonSpawn::PathWidth * 0.5f + 60.f), Doorstep.Y + TowardPlaza * Side * 420.f, 0.f);
	}
}

void AVillageDecor::BuildSignposts(bool bTailor, bool bLanterns)
{
	using namespace ErewhonSpawn;
	// À chaque sortie de la place, un panneau aux points cardinaux (nord = +X).
	for (const FExit& Exit : GExits)
	{
		const FVector2D Foot = ExitFoot(Exit, 1.f);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Exit.Dir.Y, Exit.Dir.X));
		TArray<ErewhonSigns::FArm> Arms = { { Exit.Name, Yaw } };
		// Les cabanes du bord des routes : à l'est le gérant, à l'ouest le tailleur (s'il est construit).
		if (Exit.Dir.Y > 0.f || (Exit.Dir.Y < 0.f && bTailor))
		{
			const EShopKind Shop = Exit.Dir.Y > 0.f ? EShopKind::SectorManager : EShopKind::Tailor;
			Arms.Add(ErewhonSigns::ShopArm(Shop, GetActorLocation() + FVector(Foot, 0.f), Yaw));
		}
		ErewhonSigns::AddSignpost(this, Root, FVector(Foot, 0.f), Arms, bLanterns);
	}

	// Au carrefour de chaque cabane du bord des routes : son perron d'un côté, la place de l'autre.
	for (const EShopKind Shop : { EShopKind::SectorManager, EShopKind::Tailor })
	{
		if (Shop == EShopKind::Tailor && !bTailor)
		{
			continue;
		}
		const FVector Foot = JunctionFoot(this, Shop, 1.f);
		const FVector Doorstep = GetActorTransform().InverseTransformPosition(DoorstepLocation(Shop));
		ErewhonSigns::AddSignpost(this, Root, Foot, {
			{ ShopName(Shop), Doorstep.X > 0.f ? 0.f : 180.f },
			ErewhonSigns::ShopArm(EShopKind::GeneralStore, GetActorLocation() + Foot, Doorstep.Y < 0.f ? 90.f : -90.f),
		}, bLanterns);
	}
}

void AVillageDecor::BuildLanterns(bool bTailor, bool bSignposts)
{
	using namespace ErewhonSpawn;
	// Deux lanternes en applique de part et d'autre de chaque porte.
	for (const EShopKind Shop : ShopKinds)
	{
		if (Shop == EShopKind::Tailor && !bTailor)
		{
			continue;
		}
		const FTransform Cabin = CabinTransform(Shop);
		for (const float Side : { -1.f, 1.f })
		{
			const FVector World = Cabin.TransformPosition(FVector(-CabinHalfLength - 3.f, Side * (DoorHalfWidth + 42.f), CabinFloorTop + 200.f));
			const float Yaw = Cabin.Rotator().Yaw + 180.f;   // la potence sort du mur vers l'avant
			ErewhonVillage::AddLantern(this, Root, GetActorTransform().InverseTransformPosition(World), Yaw, true);
		}
	}
	// Autour de la place.
	for (const float Angle : ErewhonVillage::GLampAngles)
	{
		ErewhonVillage::AddLantern(this, Root, ErewhonVillage::OnRing(Angle, 1100.f), Angle + 180.f, false);
	}
	// Aux carrefours : avec les panneaux, la lanterne est accrochée à leur poteau (voir BuildSignposts).
	if (bSignposts)
	{
		return;
	}
	for (const FExit& Exit : GExits)
	{
		const FVector2D Foot = ExitFoot(Exit, 1.f);
		ErewhonVillage::AddLantern(this, Root, FVector(Foot, 0.f), FMath::RadiansToDegrees(FMath::Atan2(Exit.Dir.Y, Exit.Dir.X)) + 90.f, false);
	}
	// Aux carrefours des cabanes du bord des routes.
	for (const EShopKind Shop : { EShopKind::SectorManager, EShopKind::Tailor })
	{
		if (Shop != EShopKind::Tailor || bTailor)
		{
			ErewhonVillage::AddLantern(this, Root, JunctionFoot(this, Shop, 1.f), 90.f, false);
		}
	}
}

void AVillageDecor::BuildDecoration()
{
	using namespace ErewhonSpawn;
	using namespace ErewhonVillage;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State)
	{
		return;
	}
	const TSharedRef<const FSectorLayout> Layout = GetWorld()->GetSubsystem<USectorLayoutSubsystem>()->GetLayout(FIntPoint::ZeroValue, State->GetWorldSeed());
	const FVector2D Center(GetActorLocation());
	FRandomStream Rand(1789);

	// Bordures : de petites pierres posées là où la terre rejoint l'herbe, autour de la place et le long des chemins.
	UHierarchicalInstancedStaticMeshComponent* Stones = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	Stones->SetStaticMesh(GetShape(EShape::Sphere));
	Stones->SetupAttachment(Root);
	Stones->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stones->RegisterComponent();
	AddInstanceComponent(Stones);
	Stones->SetMaterial(0, MakePaintedMaterial(Stones, TEXT("stone"), 60.f, Palette::Stone));
	TArray<FTransform> StoneInstances;
	const auto EdgeAlong = [&](const FVector2D& From, const FVector2D& Out, float MaxDistance) -> TOptional<FVector2D>
	{
		// Le long d'un rayon partant de la terre : premier point où l'herbe reprend.
		for (float D = 0.f; D < MaxDistance; D += 10.f)
		{
			const FVector2D P = From + Out * D;
			if (Layout->DirtAmount(P) < 0.5f)
			{
				return P;
			}
		}
		return {};
	};
	const auto Stone = [&](const FVector2D& P)
	{
		// Pas de bordure devant les cabanes du bord des routes : on doit pouvoir y entrer.
		for (const EShopKind Shop : { EShopKind::SectorManager, EShopKind::Tailor })
		{
			if (FVector2D::Distance(P, FVector2D(CabinTransform(Shop).GetLocation())) < ManagerClearingRadius + 150.f)
			{
				return;
			}
		}
		const FVector Local(P - Center, ErewhonTerrain::Height(P, State->GetWorldSeed()));
		StoneInstances.Add(FTransform(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), Local + FVector(0, 0, 2.f),
			FVector(Rand.FRandRange(0.16f, 0.24f), Rand.FRandRange(0.12f, 0.17f), Rand.FRandRange(0.07f, 0.1f))));
	};
	const float PlazaRadius = UErewhonSettings::Get().PlazaRadiusMeters * 100.f;
	for (float Angle = 0.f; Angle < 360.f; Angle += 1.4f)
	{
		const FVector2D Out(FMath::Cos(FMath::DegreesToRadians(Angle)), FMath::Sin(FMath::DegreesToRadians(Angle)));
		// Pas de pierres en travers des chemins qui partent de la place.
		const float Across = FMath::Min(FMath::Abs(Out.X), FMath::Abs(Out.Y)) * (PlazaRadius + 300.f);
		if (Across < PathWidth * 0.5f + 60.f)
		{
			continue;
		}
		if (const TOptional<FVector2D> P = EdgeAlong(Center + Out * (PlazaRadius * 0.6f), Out, PlazaRadius))
		{
			Stone(*P);
		}
	}
	for (const FExit& Exit : GExits)
	{
		const FVector2D Right(-Exit.Dir.Y, Exit.Dir.X);
		for (float Along = PlazaRadius + 250.f; Along < 3200.f; Along += 22.f)
		{
			for (const float Side : { -1.f, 1.f })
			{
				if (const TOptional<FVector2D> P = EdgeAlong(Center + Exit.Dir * Along, Right * Side, 600.f))
				{
					Stone(*P);
				}
			}
		}
	}
	Stones->AddInstances(StoneInstances, false, false);

	// Pots de fleurs au pied des perrons, buissons fleuris et arbres autour de la place.
	const TCHAR* Flowers[] = { TEXT("SM_flower_white"), TEXT("SM_flower_blue"), TEXT("SM_flower_purple") };
	UStaticMesh* PotMesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_flower_pot"));
	const auto Pot = [&](const FVector& Local)
	{
		if (PotMesh)
		{
			AddGeneratedMesh(this, Root, PotMesh, Local, 75.f, Rand.FRandRange(0.f, 360.f), true);
			return;
		}
		UStaticMeshComponent* Pot = AddShape(this, Root, EShape::Cylinder, Local + FVector(0, 0, 16), FVector(40, 40, 32), GTerracotta, FRotator::ZeroRotator, true);
		Pot->SetMaterial(0, MakePaintedMaterial(Pot, TEXT("roof_tiles"), 120.f, GTerracotta));
		AddShape(this, Root, EShape::Cylinder, Local + FVector(0, 0, 31), FVector(36, 36, 2), Palette::Dirt, FRotator::ZeroRotator, false);
		for (int32 i = 0; i < 4; ++i)
		{
			if (UStaticMesh* Flower = LoadGenerated<UStaticMesh>(TEXT("Meshes"), Flowers[(i + Rand.RandHelper(3)) % 3]))
			{
				const FVector Offset = OnRing(i * 90.f + 45.f, 9.f);
				AddGeneratedMesh(this, Root, Flower, Local + Offset + FVector(0, 0, 30), Rand.FRandRange(45.f, 60.f), Rand.FRandRange(0.f, 360.f), false);
			}
		}
	};
	for (const EShopKind Shop : ShopKinds)
	{
		if (Shop == EShopKind::Tailor && !HasUpgrade(this, EVillageUpgrade::Tailor))
		{
			continue;
		}
		const FTransform Cabin = CabinTransform(Shop);
		for (const float Side : { -1.f, 1.f })
		{
			Pot(GetActorTransform().InverseTransformPosition(Cabin.TransformPosition(FVector(-CabinHalfLength - 140.f, Side * 150.f, 0.f))));
		}
	}
	if (UStaticMesh* Rose = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bush_rose")))
	{
		for (const float Angle : GBushAngles)
		{
			AddGeneratedMesh(this, Root, Rose, OnRing(Angle, 1150.f), 110.f, Angle, true);
		}
	}
	UStaticMesh* Crown = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bush_hazel"));
	TArray<UStaticMesh*> TreeModels;
	for (const TCHAR* Name : { TEXT("SM_tree_oak"), TEXT("SM_tree_linden") })
	{
		if (UStaticMesh* Model = LoadGenerated<UStaticMesh>(TEXT("Meshes"), Name))
		{
			TreeModels.Add(Model);
		}
	}
	int32 TreeIndex = 0;
	for (const float Angle : GTreeAngles)
	{
		if (TreeModels.Num() > 0)
		{
			// Modèle généré (chêne, tilleul) : grand, on passe dessous ; seul le tronc arrête le joueur.
			const FVector Foot = OnRing(Angle, Rand.FRandRange(1450.f, 1650.f));
			UStaticMesh* Model = TreeModels[TreeIndex++ % TreeModels.Num()];
			AddGeneratedMesh(this, Root, Model, Foot, Rand.FRandRange(1000.f, 1200.f), Rand.FRandRange(0.f, 360.f), false);
			UStaticMeshComponent* Trunk = AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, 150), FVector(60, 60, 300), GBark, FRotator::ZeroRotator, true);
			Trunk->SetHiddenInGame(true);
			continue;
		}
		// Un grand tronc et une large couronne de feuillage (de grands buissons) qui ombrage la place.
		const FVector Foot = OnRing(Angle, Rand.FRandRange(1450.f, 1600.f));
		const float Height = Rand.FRandRange(650.f, 820.f);   // base de la couronne
		UStaticMeshComponent* Trunk = AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, Height * 0.5f), FVector(55, 55, Height), GBark, FRotator::ZeroRotator, true);
		Trunk->SetMaterial(0, MakePaintedMaterial(Trunk, TEXT("wood_trim"), 120.f, GBark, FLinearColor(0.8f, 0.7f, 0.6f)));
		for (const float Side : { -1.f, 1.f })
		{
			// Deux maîtresses branches qui partent en biais vers la couronne.
			const FVector Branch = FRotator(0.f, Angle + Side * 70.f, 0.f).Vector() * 70.f;
			AddShape(this, Root, EShape::Cylinder, Foot + Branch + FVector(0, 0, Height - 60.f), FVector(22, 22, 220), GBark,
				FRotator(0.f, Angle + Side * 70.f, 0.f) + FRotator(-35.f, 0.f, 0.f), false);
		}
		if (Crown)
		{
			AddGeneratedMesh(this, Root, Crown, Foot + FVector(0, 0, Height - 60.f), Rand.FRandRange(520.f, 600.f), Rand.FRandRange(0.f, 360.f), false);
			// Deux couronnes de touffes qui se chevauchent : une large en bas, une plus serrée au-dessus.
			for (int32 i = 0; i < 7; ++i)
			{
				const FVector Offset = OnRing(i * (360.f / 7.f) + Angle, Rand.FRandRange(190.f, 250.f));
				AddGeneratedMesh(this, Root, Crown, Foot + Offset + FVector(0, 0, Height - 180.f + Rand.FRandRange(-30.f, 50.f)),
					Rand.FRandRange(380.f, 460.f), Rand.FRandRange(0.f, 360.f), false);
			}
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector Offset = OnRing(i * 90.f + Angle + 45.f, Rand.FRandRange(90.f, 130.f));
				AddGeneratedMesh(this, Root, Crown, Foot + Offset + FVector(0, 0, Height + 80.f + Rand.FRandRange(-20.f, 40.f)),
					Rand.FRandRange(340.f, 400.f), Rand.FRandRange(0.f, 360.f), false);
			}
		}
	}

	// Les bancs : des acteurs à part, pour qu'on puisse s'y asseoir.
	for (int32 Index = 0; Index < BenchSeats().Num(); ++Index)
	{
		const FTransform Seat = BenchSeats()[Index];
		const FTransform Ground(Seat.GetRotation(), Seat.GetLocation() - FVector(0, 0, 45.f));
		if (ABench* Bench = GetWorld()->SpawnActorDeferred<ABench>(ABench::StaticClass(), Ground, this))
		{
			Bench->SeatIndex = Index;
			Bench->FinishSpawning(Ground);
			Bench->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
}

// ── Bancs ────────────────────────────────────────────────────────────────────

ABench::ABench()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void ABench::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Model = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bench")))
	{
		// Modèle Meshy : grand côté le long du banc (Y), dossier vers −X.
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetStaticMesh(Model);
		Part->SetupAttachment(Root);
		Part->SetRelativeTransform(FitToLength(Model, 160.f, 95.f) * FTransform(FRotator(0.f, BenchMeshYaw, 0.f)));
		Part->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Part->RegisterComponent();
		AddInstanceComponent(Part);
		return;
	}
	// Repère : on s'assoit face à +X ; le dossier est derrière (−X).
	const auto Plank = [this](const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator, bool bCollision = true)
	{
		UStaticMeshComponent* Part = AddShape(this, Root, EShape::Cube, Center, Size, Palette::WoodLight, Rotation, bCollision);
		Part->SetMaterial(0, MakePaintedMaterial(Part, TEXT("wood_planks"), 100.f, Palette::WoodLight));
	};
	for (const float X : { -12.f, 0.f, 12.f })
	{
		Plank(FVector(X, 0, 43), FVector(10, 150, 4));
	}
	for (const float Z : { 62.f, 78.f })
	{
		Plank(FVector(-22, 0, Z), FVector(3, 150, 10), FRotator(-10.f, 0.f, 0.f));
	}
	for (const float Y : { -64.f, 64.f })
	{
		Plank(FVector(0, Y, 20), FVector(34, 6, 40));
		Plank(FVector(-21, Y, 60), FVector(5, 6, 44), FRotator(-10.f, 0.f, 0.f), false);
	}
}

FText ABench::GetInteractionLabel() const
{
	return LOCTEXT("Sit", "S'asseoir");
}

void ABench::Interact(AErewhonPlayerController* Controller)
{
	if (AErewhonCharacter* Character = Controller ? Controller->GetPawn<AErewhonCharacter>() : nullptr)
	{
		Character->ServerSit(SeatIndex);
	}
}

#undef LOCTEXT_NAMESPACE

// ── La fontaine ──────────────────────────────────────────────────────────────

AFountain::AFountain()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AFountain::BeginPlay()
{
	Super::BeginPlay();
	using namespace ErewhonVillage;
	FTransform At = FountainTransform();
	if (const AErewhonGameState* State = AErewhonGameState::Get(this))
	{
		At.SetLocation(FVector(At.GetLocation().X, At.GetLocation().Y, ErewhonTerrain::Height(FVector2D(At.GetLocation()), State->GetWorldSeed())));
	}
	SetActorTransform(At);
	if (UStaticMesh* Model = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_fountain")))
	{
		AddGeneratedMesh(this, Root, Model, FVector::ZeroVector, FountainHeight, FountainMeshYaw, false);
		// Seul le pilier arrête le joueur : la dalle devant reste libre pour le tonneau.
		UStaticMeshComponent* Pillar = AddShape(this, Root, EShape::Cube, FVector(-18.f, FountainSide, 95.f), FVector(90.f, 80.f, 190.f), Palette::Stone, FRotator::ZeroRotator, true);
		Pillar->SetHiddenInGame(true);
		Pillar->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	else
	{
		AddShape(this, Root, EShape::Cube, FVector(-10.f, 0.f, 90.f), FVector(60.f, 70.f, 180.f), Palette::Stone, FRotator::ZeroRotator, true);
		AddShape(this, Root, EShape::Cylinder, FVector(FountainSpoutLocal.X * 0.6f, 0.f, FountainSpoutLocal.Z), FVector(6.f, 6.f, 40.f), FLinearColor::FromSRGBColor(FColor(170, 130, 60)), FRotator(90.f, 0.f, 0.f), false);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Sans modèle, un filet d'eau du bec à la dalle (ou dans le tonneau) ; le modèle a le sien.
	if (!LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_fountain")))
	{
		Stream = AddShape(this, Root, EShape::Cylinder, FVector::ZeroVector, FVector(4.f, 4.f, 100.f), FLinearColor::FromSRGBColor(FColor(160, 210, 240)), FRotator::ZeroRotator, false);
	}
	if (Stream)
	{
		Stream->SetCastShadow(false);
	}
	if (USoundWave* Sound = ErewhonSound::Load(TEXT("amb_water_pour")))
	{
		Pour = NewObject<UAudioComponent>(this);
		Pour->SetupAttachment(Root);
		Pour->SetRelativeLocation(FountainSpoutLocal);
		Pour->SetSound(Sound);
		Pour->AttenuationSettings = ErewhonSound::Attenuation(150.f, 1500.f);
		Pour->RegisterComponent();
		Pour->Play(FMath::FRandRange(0.f, 5.f));
	}
}

void AFountain::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	using namespace ErewhonVillage;
	Time += DeltaSeconds;
	if (Stream)
	{
		// Du bec jusqu'au haut du tonneau posé dessous, sinon jusqu'à la dalle ; le filet frémit.
		bool bBarrel = false;
		const FVector Spot = ErewhonVillage::FountainBarrelSpot();
		for (TActorIterator<AStorageActor> It(GetWorld()); It && !bBarrel; ++It)
		{
			bBarrel = It->GetKind() == EStorageKind::Barrel && !It->GetCarrier() && FVector::Dist2D(It->GetActorLocation(), Spot) < 80.f;
		}
		const FVector From = FountainSpoutLocal;
		const FVector To = bBarrel ? FVector(54.f, FountainSide, 96.f) : FVector(44.f, FountainSide, 12.f);
		const FVector Span = To - From;
		const float Wobble = 1.f + 0.12f * FMath::Sin(Time * 23.f) + 0.08f * FMath::Sin(Time * 37.f);
		Stream->SetRelativeLocationAndRotation((From + To) * 0.5f, FRotationMatrix::MakeFromZ(Span.GetSafeNormal()).Rotator());
		Stream->SetRelativeScale3D(FVector(0.04f * Wobble, 0.04f / Wobble, Span.Size() / 100.f));
	}
	if (Pour)
	{
		Pour->SetVolumeMultiplier(0.55f * ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Ambient));
	}
}
