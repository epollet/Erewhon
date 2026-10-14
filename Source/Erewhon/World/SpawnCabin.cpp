#include "World/SpawnCabin.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "Components/PointLightComponent.h"
#include "Core/ErewhonPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Core/ErewhonSettings.h"
#include "World/ErewhonVisuals.h"
#include "World/ShopCat.h"
#include "World/SectorTypes.h"
#include "World/SpawnLayout.h"
#include "World/Apothecary.h"
#include "World/Terrain.h"
#include "Core/ErewhonGameState.h"
#include "World/Bhurak.h"
#include "World/Storage.h"

#define LOCTEXT_NAMESPACE "ErewhonCabin"

using namespace ErewhonVisuals;

namespace
{
	constexpr float GFloorTop = ErewhonSpawn::CabinFloorTop;
	constexpr float GWallThickness = ErewhonSpawn::CabinWallThickness;
	constexpr float GDoorHalfWidth = ErewhonSpawn::DoorHalfWidth;
	constexpr float GDoorHeight = ErewhonSpawn::DoorHeight;
	constexpr float GWindowHalfWidth = 100.f;
	constexpr float GWindowSill = 100.f;
	constexpr float GWindowTop = 200.f;
	constexpr float GRoofOverhang = 50.f;

	/** Habillage d'une cabane : chaque commerce a le sien, pour éviter la répétition. */
	struct FCabinStyle
	{
		const TCHAR* Wall = TEXT("wood_planks");
		float WallTile = 220.f;
		FLinearColor WallColor = Palette::WoodLight;
		const TCHAR* Roof = TEXT("roof_tiles");
		float RoofTile = 240.f;
		FLinearColor RoofColor = Palette::Roof;
		float RoofSlope = 30.f;   // degrés
		const TCHAR* Floor = TEXT("stone_slabs");
		float FloorTile = 220.f;
		bool bTimberFrame = false;   // colombages sombres sur un enduit clair
	};

	FCabinStyle StyleFor(EShopKind Kind)
	{
		FCabinStyle Style;
		if (Kind == EShopKind::Tailor)
		{
			// Maisonnette enduite à colombages, toit d'ardoise plus pentu, plancher de chêne.
			Style.Wall = TEXT("plaster");
			Style.WallTile = 260.f;
			Style.WallColor = FLinearColor::FromSRGBColor(FColor(236, 226, 204));
			Style.Roof = TEXT("roof_slate");
			Style.RoofTile = 200.f;
			Style.RoofColor = FLinearColor::FromSRGBColor(FColor(110, 128, 140));
			Style.RoofSlope = 38.f;
			Style.Floor = TEXT("floor_boards");
			Style.FloorTile = 200.f;
			Style.bTimberFrame = true;
		}
		else if (Kind == EShopKind::Breeder)
		{
			// Grande ferme de planches peintes en rouge, toit de chaume épais, plancher de bois.
			Style.Wall = TEXT("barn_red");
			Style.WallTile = 300.f;
			Style.WallColor = FLinearColor::FromSRGBColor(FColor(160, 70, 52));
			Style.Roof = TEXT("thatch");
			Style.RoofTile = 300.f;
			Style.RoofColor = FLinearColor::FromSRGBColor(FColor(214, 178, 96));
			Style.RoofSlope = 42.f;
			Style.Floor = TEXT("floor_boards");
			Style.FloorTile = 200.f;
		}
		else if (Kind == EShopKind::Workshop)
		{
			// Vieil atelier de bois goudronné, toit d'ardoise sombre peu pentu, sol de dalles.
			Style.Wall = TEXT("dark_timber");
			Style.WallTile = 260.f;
			Style.WallColor = FLinearColor::FromSRGBColor(FColor(70, 52, 40));
			Style.Roof = TEXT("roof_slate");
			Style.RoofTile = 200.f;
			Style.RoofColor = FLinearColor::FromSRGBColor(FColor(86, 92, 100));
			Style.RoofSlope = 26.f;
			Style.Floor = TEXT("stone_slabs");
			Style.FloorTile = 220.f;
		}
		else if (Kind == EShopKind::Apothecary)
		{
			// Chaumière de pierre des champs, toit d'ardoise moussue, plancher de bois.
			Style.Wall = TEXT("stone");
			Style.WallTile = 480.f;   // de grosses pierres des champs
			Style.WallColor = Palette::Stone;
			Style.Roof = TEXT("roof_slate");
			Style.RoofTile = 200.f;
			Style.RoofColor = FLinearColor::FromSRGBColor(FColor(100, 118, 112));
			Style.RoofSlope = 36.f;
			Style.Floor = TEXT("floor_boards");
			Style.FloorTile = 200.f;
		}
		return Style;
	}

	const FLinearColor GParchment = FLinearColor::FromSRGBColor(FColor(232, 214, 170));
	const FLinearColor GBrass = FLinearColor::FromSRGBColor(FColor(196, 156, 72));
}

// ── Marchand ─────────────────────────────────────────────────────────────────

AMerchant::AMerchant()
{
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(40.f, 90.f);
	Capsule->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RootComponent = Capsule;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
}

void AMerchant::Play(FName Name, bool bLoop)
{
	const TObjectPtr<UAnimSequence>* Sequence = Animations.Find(Name);
	if (!Animated || !Sequence || !*Sequence)
	{
		return;
	}
	Playing = Name;
	Animated->PlayAnimation(Sequence->Get(), bLoop);
	PlayingUntil = bLoop ? 0.0 : GetWorld()->GetTimeSeconds() + (*Sequence)->GetPlayLength();
}

void AMerchant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Animated)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// Un geste terminé : retour à l'attente ordinaire.
	if (PlayingUntil > 0.0 && Now >= PlayingUntil)
	{
		PlayingUntil = 0.0;
		Play(TEXT("idle"), true);
	}
	if (PlayingUntil > 0.0)
	{
		return;
	}
	// Quelqu'un arrive (le joueur de cette machine) : un salut de la main, pas plus d'une fois par minute.
	const APlayerController* Local = GetWorld()->GetFirstPlayerController();
	const APawn* Visitor = Local ? Local->GetPawn() : nullptr;
	if (bWaves && Visitor && Now - LastWave > 60.0 && FVector::Dist2D(Visitor->GetActorLocation(), GetActorLocation()) < 450.f && Animations.Contains(TEXT("wave")))
	{
		LastWave = Now;
		Play(TEXT("wave"), false);
		return;
	}
	// De temps en temps, une autre attente : se dandiner, regarder autour, parler tout seul.
	if (Now >= NextFidget)
	{
		NextFidget = Now + FMath::FRandRange(7.f, 16.f);
		const FName Fidgets[] = { TEXT("idle2"), TEXT("idle3"), TEXT("look"), TEXT("idle2") };
		const FName Pick = Fidgets[FMath::RandHelper(UE_ARRAY_COUNT(Fidgets))];
		if (Animations.Contains(Pick))
		{
			Play(Pick, false);
		}
	}
}

void AMerchant::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* MeshName = Kind == EShopKind::SectorManager ? TEXT("SM_sector_manager")
		: Kind == EShopKind::Tailor ? TEXT("SM_tailor") : Kind == EShopKind::Breeder ? TEXT("SM_breeder")
		: Kind == EShopKind::Workshop ? TEXT("SM_craftsman") : TEXT("SM_merchant");
	// Le personnage animé, s'il a été importé (Characters/<nom>/SK_<nom> et ses A_<nom>_<geste>) ;
	// de préférence sa version modelée en T-pose (<nom>_tp), que le squelette automatique rend bien mieux.
	FString Character = FString(MeshName).RightChop(3);
	if (LoadGenerated<USkeletalMesh>(*FString::Printf(TEXT("Characters/%s_tp"), *Character), FString::Printf(TEXT("SK_%s_tp"), *Character)))
	{
		Character += TEXT("_tp");
		bWaves = true;
	}
	if (USkeletalMesh* Skeletal = LoadGenerated<USkeletalMesh>(*FString::Printf(TEXT("Characters/%s"), *Character), FString::Printf(TEXT("SK_%s"), *Character)))
	{
		Animated = NewObject<USkeletalMeshComponent>(this, TEXT("Animated"));
		Animated->SetupAttachment(Capsule);
		Animated->SetRelativeLocationAndRotation(FVector(0, 0, -Capsule->GetScaledCapsuleHalfHeight()), FRotator(0.f, MerchantMeshYaw, 0.f));
		Animated->SetSkeletalMesh(Skeletal);
		Animated->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Animated->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Animated->RegisterComponent();
		AddInstanceComponent(Animated);
		for (const TCHAR* Gesture : { TEXT("idle"), TEXT("idle2"), TEXT("idle3"), TEXT("look"), TEXT("wave"), TEXT("talk") })
		{
			if (UAnimSequence* Sequence = LoadGenerated<UAnimSequence>(*FString::Printf(TEXT("Characters/%s"), *Character), FString::Printf(TEXT("A_%s_%s"), *Character, Gesture)))
			{
				Animations.Add(Gesture, Sequence);
			}
		}
		Play(TEXT("idle"), true);
		// Pas tous en même temps : chacun commence à son rythme.
		NextFidget = GetWorld()->GetTimeSeconds() + FMath::FRandRange(3.f, 12.f);
		return;
	}
	if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), MeshName))
	{
		// Modèle Meshy, posé sur le plancher (bas de la capsule).
		AddGeneratedMesh(this, Capsule, Mesh, FVector(0, 0, -Capsule->GetScaledCapsuleHalfHeight()), Kind == EShopKind::Tailor ? 165.f : 175.f,
			MerchantMeshYaw, false);
		return;
	}
	// Silhouette provisoire : tablier vert, tête, chapeau de paille.
	const FLinearColor Apron = FLinearColor::FromSRGBColor(FColor(92, 140, 96));
	const FLinearColor Straw = FLinearColor::FromSRGBColor(FColor(232, 200, 120));
	AddShape(this, Capsule, EShape::Cylinder, FVector(0, 0, -25), FVector(70, 70, 130), Apron, FRotator::ZeroRotator, false);
	AddShape(this, Capsule, EShape::Sphere, FVector(0, 0, 65), FVector(42, 42, 46), Palette::Skin, FRotator::ZeroRotator, false);
	AddShape(this, Capsule, EShape::Cylinder, FVector(0, 0, 86), FVector(80, 80, 4), Straw, FRotator::ZeroRotator, false);
	AddShape(this, Capsule, EShape::Cone, FVector(0, 0, 87), FVector(40, 40, 22), Straw, FRotator::ZeroRotator, false);
}

FText AMerchant::GetInteractionLabel() const
{
	switch (Kind)
	{
	case EShopKind::SectorManager: return LOCTEXT("TalkToManager", "Parler au maire");
	case EShopKind::Tailor:        return LOCTEXT("TalkToTailor", "Parler à la couturière");
	case EShopKind::Breeder:       return LOCTEXT("TalkToBreeder", "Parler à l'éleveur");
	case EShopKind::Workshop:      return LOCTEXT("TalkToCraftsman", "Parler à l'artisane");
	default:                       return LOCTEXT("TalkToMerchant", "Parler au marchand");
	}
}

void AMerchant::Interact(AErewhonPlayerController* Controller)
{
	if (Controller)
	{
		Controller->OpenStore(Kind);
	}
}

// ── Cabane ───────────────────────────────────────────────────────────────────

ASpawnCabin::ASpawnCabin()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Lamp = CreateDefaultSubobject<UPointLightComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Root);
	Lamp->SetRelativeLocation(FVector(0, 0, GFloorTop + 240.f));
	Lamp->SetIntensityUnits(ELightUnits::Lumens);
	Lamp->SetIntensity(1500.f);
	Lamp->SetAttenuationRadius(900.f);
	Lamp->SetLightColor(FLinearColor::FromSRGBColor(FColor(255, 196, 120)));
	Lamp->SetSourceRadius(15.f);
}

void ASpawnCabin::BeginPlay()
{
	Super::BeginPlay();
	HalfLength = ErewhonSpawn::CabinHalfSize(Kind).X;
	HalfWidth = ErewhonSpawn::CabinHalfSize(Kind).Y;
	WallHeight = ErewhonSpawn::CabinWallHeightFor(Kind);
	if (Kind == EShopKind::Apothecary)
	{
		// Au milieu d'une prairie vallonnée : le remblai descend jusqu'au point le plus bas de l'emprise.
		// Pas de lampe au plafond : c'est la cheminée qui éclaire.
		Lamp->SetVisibility(false);
		if (const AErewhonGameState* State = AErewhonGameState::Get(this))
		{
			float Lowest = GetActorLocation().Z;
			for (int32 i = 0; i < 16; ++i)
			{
				const float Angle = UE_TWO_PI * i / 16.f;
				const FVector Local(FMath::Cos(Angle) * (ErewhonSpawn::CabinHalfLength + 180.f), FMath::Sin(Angle) * (ErewhonSpawn::CabinHalfWidth + 120.f), 0.f);
				Lowest = FMath::Min(Lowest, ErewhonTerrain::Height(FVector2D(GetActorTransform().TransformPosition(Local)), State->GetWorldSeed()));
			}
			FoundationDepth = GetActorLocation().Z - Lowest + 25.f;
		}
		BuildCabin();
		return;
	}
	SetActorTransform(ErewhonSpawn::CabinTransform(Kind));
	Lamp->SetRelativeLocation(FVector(0, 0, GFloorTop + WallHeight - 60.f));
	BuildCabin();

	// Le marchand se tient derrière son comptoir, face à la porte (−X de la cabane).
	const FVector MerchantLocation = ErewhonSpawn::MerchantLocation(Kind) + FVector(0, 0, GFloorTop + 90.f);
	const FTransform Spawn(GetActorRotation() + FRotator(0, 180, 0), MerchantLocation);
	if (AMerchant* Merchant = GetWorld()->SpawnActorDeferred<AMerchant>(AMerchant::StaticClass(), Spawn, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Merchant->Kind = Kind;
		Merchant->FinishSpawning(Spawn);
	}
}

void ASpawnCabin::AddFitted(UStaticMesh* Mesh, const FTransform& Fit, const FVector& Location, float Yaw, bool bCollision)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(Mesh);
	Part->SetupAttachment(Root);
	Part->SetRelativeTransform(Fit * FTransform(FRotator(0.f, Yaw, 0.f), Location));
	Part->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Part->RegisterComponent();
	AddInstanceComponent(Part);
}

UStaticMeshComponent* ASpawnCabin::AddBox(USceneComponent* Parent, const FVector& Min, const FVector& Max, const FLinearColor& Color,
	const TCHAR* Texture, float Tile, bool bCollision)
{
	UStaticMeshComponent* Part = AddShape(this, Parent, EShape::Cube, (Min + Max) * 0.5f, Max - Min, Color, FRotator::ZeroRotator, bCollision);
	if (Texture)
	{
		Part->SetMaterial(0, MakePaintedMaterial(Part, Texture, Tile, Color));
	}
	return Part;
}

void ASpawnCabin::BuildCabin()
{
	using namespace ErewhonSpawn;
	const float HL = HalfLength;
	const float HW = HalfWidth;
	const float H = WallHeight;
	const float T = GWallThickness;
	const FCabinStyle Style = StyleFor(Kind);

	// Matériaux peints générés selon le style de la maison, avec la couleur unie en repli.
	const auto Paint = [&Style](UStaticMeshComponent* Part, const FLinearColor& Color)
	{
		const TCHAR* Texture = Style.Wall;
		float Tile = Style.WallTile;
		FLinearColor Fallback = Style.WallColor;
		if (Color == Palette::Stone)
		{
			Texture = TEXT("stone");
			Tile = 160.f;
			Fallback = Color;
		}
		else if (Color == Palette::Roof)
		{
			Texture = Style.Roof;
			Tile = Style.RoofTile;
			Fallback = Style.RoofColor;
		}
		else if (Color == Palette::WoodDark)
		{
			Texture = TEXT("wood_trim");
			Tile = 160.f;
			Fallback = Color;
		}
		Part->SetMaterial(0, MakePaintedMaterial(Part, Texture, Tile, Fallback));
		return Part;
	};

	// Une boîte de mur entre deux bornes, en coordonnées locales (Z relatif au plancher).
	const auto Box = [this, &Paint](const FVector& Min, const FVector& Max, const FLinearColor& Color)
	{
		const FVector Center = (Min + Max) * 0.5f + FVector(0, 0, GFloorTop);
		Paint(AddShape(this, Root, EShape::Cube, Center, Max - Min, Color), Color);
	};

	// Remblai de fondation : un tertre de pierre au contour irrégulier, aux flancs inclinés.
	{
		constexpr int32 Points = 48;
		TArray<FVector2D> Top, Bottom;
		for (int32 i = 0; i < Points; ++i)
		{
			// Rectangle arrondi (superellipse), bosselé par un bruit sur le pourtour.
			const float Angle = UE_TWO_PI * i / Points;
			const float C = FMath::Cos(Angle), S = FMath::Sin(Angle);
			const float Round = FMath::Pow(FMath::Pow(FMath::Abs(C), 8.f) + FMath::Pow(FMath::Abs(S), 8.f), -0.125f);
			const float Bump = 1.f + 0.06f * FMath::PerlinNoise1D(Angle * 2.3f + 4.7f);
			const FVector2D Dir(C * Round * Bump, S * Round * Bump);
			Top.Add(Dir * FVector2D(HL + 55.f, HW + 55.f));
			Bottom.Add(Dir * FVector2D(HL + 115.f, HW + 115.f));
		}
		AddLoft(this, Root, Bottom, -FoundationDepth, Top, GFloorTop, MakePaintedMaterial(this, TEXT("stone"), 160.f, Palette::Stone));
	}

	// Sol intérieur : dalles de pierre, ou plancher selon la maison.
	UStaticMeshComponent* Floor = AddShape(this, Root, EShape::Cube, FVector(0, 0, GFloorTop + 1.f), FVector(2 * (HL - T), 2 * (HW - T), 2.f), Palette::Stone);
	Floor->SetMaterial(0, MakePaintedMaterial(Floor, Style.Floor, Style.FloorTile, Palette::Stone));

	// Perron d'entrée en bois : un palier qui recouvre tout le talus, puis deux marches
	// posées au-delà du pied du remblai. Limons de chaque côté.
	{
		constexpr float StairHalfWidth = 95.f;
		constexpr float Tread = 34.f;
		const float Landing = -HL - 105.f;   // au-delà du pied du talus (−HL − 115 au plus loin)
		const auto Wood = [this](const FVector& Min, const FVector& Max, bool bDark)
		{
			UStaticMeshComponent* Part = AddShape(this, Root, EShape::Cube, (Min + Max) * 0.5f, Max - Min, bDark ? Palette::WoodDark : Palette::WoodLight);
			Part->SetMaterial(0, bDark
				? MakePaintedMaterial(Part, TEXT("wood_trim"), 140.f, Palette::WoodDark)
				: MakePaintedMaterial(Part, TEXT("wood_planks"), 120.f, Palette::WoodLight));
		};
		Wood(FVector(Landing, -StairHalfWidth, -FoundationDepth - 5.f), FVector(-HL + T, StairHalfWidth, GFloorTop + 1.f), false);
		for (int32 Step = 1; Step <= 2; ++Step)
		{
			const float Top = GFloorTop * (3 - Step) / 3.f;
			const float X1 = Landing - (Step - 1) * Tread;
			Wood(FVector(X1 - Tread, -StairHalfWidth, -FoundationDepth + 10.f), FVector(X1, StairHalfWidth, Top), false);
		}
		for (const float Side : { -1.f, 1.f })
		{
			const float Y = Side * (StairHalfWidth + 5.f);
			Wood(FVector(Landing - 2 * Tread - 4.f, Y - 5.f, -FoundationDepth - 5.f), FVector(-HL + T, Y + 5.f, GFloorTop + 8.f), true);
		}
	}

	// Mur du fond (+X), plein.
	Box(FVector(HL - T, -HW, 0), FVector(HL, HW, H), Palette::WoodLight);

	// Mur de la porte (−X).
	Box(FVector(-HL, -HW, 0), FVector(-HL + T, -GDoorHalfWidth, H), Palette::WoodLight);
	Box(FVector(-HL, GDoorHalfWidth, 0), FVector(-HL + T, HW, H), Palette::WoodLight);
	Box(FVector(-HL, -GDoorHalfWidth, GDoorHeight), FVector(-HL + T, GDoorHalfWidth, H), Palette::WoodLight);

	// Murs latéraux, chacun percé d'une fenêtre centrée ; avec la remise, une porte remplace celle de droite.
	for (const float Side : { -1.f, 1.f })
	{
		const float Y0 = Side > 0 ? HW - T : -HW;
		const float Y1 = Side > 0 ? HW : -HW + T;
		if (Side > 0 && bStoreroom && Kind == EShopKind::Apothecary)
		{
			Box(FVector(-HL, Y0, 0), FVector(-GDoorHalfWidth, Y1, H), Palette::WoodLight);
			Box(FVector(GDoorHalfWidth, Y0, 0), FVector(HL, Y1, H), Palette::WoodLight);
			Box(FVector(-GDoorHalfWidth, Y0, GDoorHeight), FVector(GDoorHalfWidth, Y1, H), Palette::WoodLight);
			// Le chambranle, des deux côtés.
			for (const float X : { -GDoorHalfWidth - 6.f, GDoorHalfWidth + 6.f })
			{
				Box(FVector(X - 6.f, Y0 - 4.f, 0), FVector(X + 6.f, Y1 + 4.f, GDoorHeight + 8.f), Palette::WoodDark);
			}
			Box(FVector(-GDoorHalfWidth - 12.f, Y0 - 4.f, GDoorHeight), FVector(GDoorHalfWidth + 12.f, Y1 + 4.f, GDoorHeight + 12.f), Palette::WoodDark);
			continue;
		}
		Box(FVector(-HL, Y0, 0), FVector(-GWindowHalfWidth, Y1, H), Palette::WoodLight);
		Box(FVector(GWindowHalfWidth, Y0, 0), FVector(HL, Y1, H), Palette::WoodLight);
		Box(FVector(-GWindowHalfWidth, Y0, 0), FVector(GWindowHalfWidth, Y1, GWindowSill), Palette::WoodLight);
		Box(FVector(-GWindowHalfWidth, Y0, GWindowTop), FVector(GWindowHalfWidth, Y1, H), Palette::WoodLight);
		BuildWindow(Side, Y0, Y1);
	}

	// Chambranle de la porte, côté extérieur.
	Box(FVector(-HL - 6, -GDoorHalfWidth - 12, 0), FVector(-HL + T + 6, -GDoorHalfWidth, GDoorHeight + 12), Palette::WoodDark);
	Box(FVector(-HL - 6, GDoorHalfWidth, 0), FVector(-HL + T + 6, GDoorHalfWidth + 12, GDoorHeight + 12), Palette::WoodDark);
	Box(FVector(-HL - 6, -GDoorHalfWidth - 12, GDoorHeight), FVector(-HL + T + 6, GDoorHalfWidth + 12, GDoorHeight + 12), Palette::WoodDark);

	// Poteaux d'angle plus sombres.
	for (const float SX : { -1.f, 1.f })
	{
		for (const float SY : { -1.f, 1.f })
		{
			const FVector Corner(SX * (HL - T * 0.5f), SY * (HW - T * 0.5f), 0);
			Box(Corner - FVector(T, T, 0), Corner + FVector(T, T, H + 10), Palette::WoodDark);
		}
	}
	if (Style.bTimberFrame)
	{
		BuildTimberFrame();
	}

	// Toit à deux pans, faîtage dans l'axe de la cabane.
	const float HalfSpan = HW + GRoofOverhang;
	const float SlopeRad = FMath::DegreesToRadians(Style.RoofSlope);
	const float SlabWidth = HalfSpan / FMath::Cos(SlopeRad);
	const float RidgeRise = HalfSpan * FMath::Tan(SlopeRad);
	for (const float Side : { -1.f, 1.f })
	{
		const FVector Center(0, Side * HalfSpan * 0.5f, GFloorTop + H + RidgeRise * 0.5f + 8.f);
		Paint(AddShape(this, Root, EShape::Cube, Center, FVector(2 * HL + 2 * GRoofOverhang, SlabWidth, 16), Palette::Roof,
			FRotator(0, 0, Side * Style.RoofSlope)), Palette::Roof);
	}

	// La remise : une pièce de plain-pied accolée au mur de droite, sous un toit en appentis.
	if (bStoreroom && Kind == EShopKind::Apothecary)
	{
		const float L = ErewhonApothecary::StoreroomHalfLength;
		const float D = ErewhonApothecary::StoreroomDepth;
		constexpr float AH = 230.f;   // hauteur des murs de la remise
		UStaticMeshComponent* Base = AddShape(this, Root, EShape::Cube, FVector(0.f, HW + D * 0.5f, (GFloorTop - FoundationDepth) * 0.5f),
			FVector(2 * L + 30.f, D + 20.f, GFloorTop + FoundationDepth), Palette::Stone);
		Base->SetMaterial(0, MakePaintedMaterial(Base, TEXT("stone"), 160.f, Palette::Stone));
		UStaticMeshComponent* Boards = AddShape(this, Root, EShape::Cube, FVector(0.f, HW + D * 0.5f, GFloorTop + 1.f), FVector(2 * (L - T), D, 2.f), Palette::WoodLight);
		Boards->SetMaterial(0, MakePaintedMaterial(Boards, Style.Floor, Style.FloorTile, Palette::WoodLight));
		// Le seuil de la porte, à travers l'épaisseur du mur.
		UStaticMeshComponent* Sill = AddShape(this, Root, EShape::Cube, FVector(0.f, HW - T * 0.5f, GFloorTop + 1.f), FVector(2 * GDoorHalfWidth, T + 6.f, 2.f), Palette::WoodLight);
		Sill->SetMaterial(0, MakePaintedMaterial(Sill, Style.Floor, Style.FloorTile, Palette::WoodLight));
		Box(FVector(-L, HW + D - T, 0), FVector(L, HW + D, AH), Palette::WoodLight);
		for (const float SX : { -1.f, 1.f })
		{
			Box(FVector(SX > 0 ? L - T : -L, HW, 0), FVector(SX > 0 ? L : -L + T, HW + D, AH), Palette::WoodLight);
			Box(FVector(SX * L - T, HW + D - T, 0), FVector(SX * L + T, HW + D + T, AH + 6.f), Palette::WoodDark);   // poteau d'angle
		}
		// Les pignons du toit en appentis, au-dessus des murs de côté.
		UMaterialInterface* WallMaterial = MakePaintedMaterial(this, Style.Wall, Style.WallTile, Style.WallColor);
		const TArray<FVector2D> Lean = { FVector2D(HW, GFloorTop + AH), FVector2D(HW + D, GFloorTop + AH), FVector2D(HW, GFloorTop + H - 6.f) };
		AddPrism(this, Root, Lean, L - T, L, WallMaterial);
		AddPrism(this, Root, Lean, -L, -L + T, WallMaterial);
		// Le toit : du dessous de l'avant-toit de la maison jusqu'au-delà du mur du fond.
		const float Run = D + 40.f, Rise = H - 6.f - AH;
		const float Slope = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));
		Paint(AddShape(this, Root, EShape::Cube, FVector(0.f, HW + Run * 0.5f, GFloorTop + AH + Rise * 0.5f + 8.f),
			FVector(2 * L + 40.f, FMath::Sqrt(Run * Run + Rise * Rise), 14.f), Palette::Roof, FRotator(0.f, 0.f, Slope)), Palette::Roof);
		// Un plafond de planches sous le toit, et une lampe à huile : la pièce est vide, prête à meubler.
		AddBox(Root, FVector(-L + T, HW, GFloorTop + AH - 8.f), FVector(L - T, HW + D - T, GFloorTop + AH), Palette::WoodLight, TEXT("wood_planks"), 220.f);
		for (const float Y : { HW + D * 0.33f, HW + D * 0.66f })
		{
			Box(FVector(-L + T, Y - 9.f, AH - 26.f), FVector(L - T, Y + 9.f, AH - 8.f), Palette::WoodDark);   // solives
		}
		UPointLightComponent* StoreLamp = NewObject<UPointLightComponent>(this);
		StoreLamp->SetupAttachment(Root);
		StoreLamp->SetRelativeLocation(FVector(0.f, HW + D * 0.5f, GFloorTop + AH - 40.f));
		StoreLamp->SetIntensityUnits(ELightUnits::Lumens);
		StoreLamp->SetIntensity(500.f);
		StoreLamp->SetAttenuationRadius(500.f);
		StoreLamp->SetLightColor(FLinearColor::FromSRGBColor(FColor(255, 196, 120)));
		StoreLamp->RegisterComponent();
		AddInstanceComponent(StoreLamp);
	}

	// Hauteur du dessous du toit à l'aplomb des murs longs : la sablière comble ce jour.
	const float EaveGap = RidgeRise * GRoofOverhang / HalfSpan;
	for (const float Side : { -1.f, 1.f })
	{
		const float Y0 = Side > 0 ? HW - T : -HW;
		Box(FVector(-HL, Y0, H), FVector(HL, Y0 + T, H + EaveGap + 6.f), Palette::WoodDark);
	}

	// Pignons pleins aux deux bouts : un pentagone qui épouse le dessous du toit.
	UMaterialInterface* GableMaterial = MakePaintedMaterial(this, Style.Wall, Style.WallTile, Style.WallColor);
	const TArray<FVector2D> Gable = {
		FVector2D(-HW, GFloorTop + H),
		FVector2D(HW, GFloorTop + H),
		FVector2D(HW, GFloorTop + H + EaveGap),
		FVector2D(0.f, GFloorTop + H + RidgeRise - 2.f),
		FVector2D(-HW, GFloorTop + H + EaveGap),
	};
	AddPrism(this, Root, Gable, HL - T, HL, GableMaterial);
	AddPrism(this, Root, Gable, -HL, -HL + T, GableMaterial);

	// Plafond de planches (plancher du grenier) : de l'intérieur, on ne voit plus le toit.
	AddBox(Root, FVector(-HL + T, -HW + T, GFloorTop + H - 10.f), FVector(HL - T, HW - T, GFloorTop + H), Palette::WoodLight, TEXT("wood_planks"), 220.f);
	// Poutres apparentes sous le plafond.
	for (const float X : { -HL * 0.5f, 0.f, HL * 0.5f })
	{
		Box(FVector(X - 10.f, -HW + T, H - 32.f), FVector(X + 10.f, HW - T, H - 10.f), Palette::WoodDark);
	}

	// Enseigne peinte au-dessus de la porte, sur un fond de bois sombre.
	{
		const FVector Sign(-HL - 4.4f, 0.f, GFloorTop + 280.f);
		const TCHAR* Texture = Kind == EShopKind::SectorManager ? TEXT("T_sign_town_hall")
			: Kind == EShopKind::Tailor ? TEXT("T_sign_tailor")
			: Kind == EShopKind::Apothecary ? TEXT("T_sign_apothecary") : Kind == EShopKind::Breeder ? TEXT("T_sign_breeder")
			: Kind == EShopKind::Workshop ? TEXT("T_sign_workshop") : TEXT("T_sign_general_store");
		if (AddPicture(this, Root, Sign, 200.f, 84.f, 0.f, Texture))
		{
			Box(FVector(-HL - 3.8f, -106.f, 280.f - 48.f), FVector(-HL, 106.f, 280.f + 48.f), Palette::WoodDark);
		}
	}

	switch (Kind)
	{
	case EShopKind::SectorManager:
		BuildManagerInterior();
		break;
	case EShopKind::Tailor:
		BuildTailorInterior();
		break;
	case EShopKind::Apothecary:
		BuildApothecaryInterior();
		break;
	case EShopKind::Breeder:
		BuildBreederInterior();
		BuildBarn();
		break;
	case EShopKind::Workshop:
		BuildWorkshopInterior();
		BuildYard();
		break;
	default:
		BuildStoreInterior();
		break;
	}
}

void ASpawnCabin::BuildWindow(float Side, float Y0, float Y1)
{
	// Une vraie fenêtre de cottage : appui saillant, linteau, encadrement, croisillons et carreaux,
	// volets ouverts à plat contre le mur et jardinière fleurie.
	const float W = GWindowHalfWidth;
	const float Outer = Side > 0 ? Y1 : Y0;          // face extérieure du mur
	const float Out = Side;                          // vers l'extérieur
	const auto Wood = [this](const FVector& A, const FVector& B)
	{
		const FVector Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z) + GFloorTop);
		const FVector Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z) + GFloorTop);
		AddBox(Root, Min, Max, Palette::WoodDark, TEXT("wood_trim"), 120.f, false);
	};

	// Encadrement qui déborde de part et d'autre du mur.
	const float F0 = Y0 - 4.f;
	const float F1 = Y1 + 4.f;
	Wood(FVector(-W - 10, F0, GWindowTop), FVector(W + 10, F1, GWindowTop + 10));
	Wood(FVector(-W - 10, F0, GWindowSill), FVector(-W, F1, GWindowTop));
	Wood(FVector(W, F0, GWindowSill), FVector(W + 10, F1, GWindowTop));
	// Appui : une planche épaisse qui avance dehors, et le linteau au-dessus.
	Wood(FVector(-W - 16, Y0 - 2.f, GWindowSill - 8), FVector(W + 16, Y1 + 2.f, GWindowSill));
	Wood(FVector(-W - 16, Outer, GWindowSill - 12), FVector(W + 16, Outer + Out * 14.f, GWindowSill - 2));
	Wood(FVector(-W - 20, Outer, GWindowTop + 10), FVector(W + 20, Outer + Out * 6.f, GWindowTop + 24));

	// Châssis à petits carreaux, au milieu de l'épaisseur du mur.
	const float MidY = (Y0 + Y1) * 0.5f;
	const float MidZ = (GWindowSill + GWindowTop) * 0.5f;
	Wood(FVector(-2.5f, MidY - 3, GWindowSill), FVector(2.5f, MidY + 3, GWindowTop));
	Wood(FVector(-W, MidY - 3, MidZ - 2.5f), FVector(W, MidY + 3, MidZ + 2.5f));
	for (const float X : { -W * 0.5f, W * 0.5f })
	{
		Wood(FVector(X - 1.5f, MidY - 2, GWindowSill), FVector(X + 1.5f, MidY + 2, GWindowTop));
	}
	if (UMaterialInterface* Glass = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Glass")))
	{
		UStaticMeshComponent* Pane = AddShape(this, Root, EShape::Cube, FVector(0, MidY, GFloorTop + MidZ),
			FVector(2 * W, 1.f, GWindowTop - GWindowSill), FLinearColor::White, FRotator::ZeroRotator, false);
		Pane->SetMaterial(0, Glass);
		Pane->SetCastShadow(false);
	}

	// Volets : modèle dédié, l'un en miroir de l'autre pour que les deux poignées soient côté extérieur.
	const float ShutterY = Side > 0 ? Y1 + 7.f : Y0 - 11.f;
	UStaticMesh* ShutterMesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_shutter"));
	const FLinearColor ShutterColor = FLinearColor::FromSRGBColor(FColor(86, 138, 146));
	for (const float SX : { -1.f, 1.f })
	{
		const float X0 = SX > 0 ? W + 12 : -W - 12 - W;
		const FVector Center(X0 + W * 0.5f, ShutterY + 2.f, GFloorTop + MidZ);
		if (ShutterMesh)
		{
			// FitToLength met la largeur sur Y, façade vers −X : un quart de tour la tourne vers l'extérieur.
			FTransform Fit = FitToLength(ShutterMesh, W, GWindowTop - GWindowSill + 10.f);
			if (SX * Side > 0.f)
			{
				Fit = Fit * FTransform(FQuat::Identity, FVector::ZeroVector, FVector(1.f, -1.f, 1.f));
			}
			AddFitted(ShutterMesh, Fit, FVector(Center.X, Center.Y, GFloorTop + GWindowSill - 5.f), Side > 0 ? -90.f : 90.f, false);
			continue;
		}
		UStaticMeshComponent* Panel = AddShape(this, Root, EShape::Cube, Center, FVector(W, 4.f, GWindowTop - GWindowSill + 10.f), ShutterColor);
		Panel->SetMaterial(0, MakePaintedMaterial(Panel, TEXT("wood_planks"), 120.f, ShutterColor, ShutterColor * 1.6f));
	}

	// Jardinière fleurie sous la fenêtre : modèle dédié, sinon caisse et fleurs.
	const float BoxY = Side > 0 ? Y1 + 14.f : Y0 - 14.f;
	if (UStaticMesh* Planter = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_window_box")))
	{
		AddFitted(Planter, FitToLength(Planter, 2 * W + 20.f, 50.f), FVector(0.f, BoxY + Side * 4.f, GFloorTop + GWindowSill - 52.f), Side > 0 ? -90.f : 90.f, false);
		return;
	}
	Wood(FVector(-W, BoxY - 12, GWindowSill - 46), FVector(W, BoxY + 12, GWindowSill - 24));
}

void ASpawnCabin::BuildTimberFrame()
{
	// Colombages : sablières, lisses et poteaux de chêne sombre posés sur l'enduit, croix de Saint-André
	// dans les panneaux pleins. Tout est légèrement en saillie (3 cm) de la face extérieure.
	using namespace ErewhonSpawn;
	const float HL = HalfLength;
	const float HW = HalfWidth;
	const float H = WallHeight;
	constexpr float Beam = 12.f;
	constexpr float Proud = 3.f;
	constexpr float MidRail = 150.f;

	// Repère d'une façade : U le long du mur, la normale sortante, la demi-longueur.
	struct FFacade { FVector Normal; FVector Along; float Half; float Offset; float GapHalf; float GapTop; };
	const FFacade Facades[] = {
		{ FVector(0, 1, 0), FVector(1, 0, 0), HL, HW, GWindowHalfWidth + 10.f, GWindowTop + 10.f },     // fenêtre
		{ FVector(0, -1, 0), FVector(1, 0, 0), HL, HW, GWindowHalfWidth + 10.f, GWindowTop + 10.f },    // fenêtre
		{ FVector(1, 0, 0), FVector(0, 1, 0), HW, HL, 0.f, 0.f },                                        // mur du fond
		{ FVector(-1, 0, 0), FVector(0, 1, 0), HW, HL, GDoorHalfWidth + 12.f, GDoorHeight + 12.f },      // porte
	};
	UMaterialInterface* Oak = MakePaintedMaterial(this, TEXT("wood_trim"), 120.f, Palette::WoodDark);
	for (const FFacade& F : Facades)
	{
		const auto Piece = [&](float U0, float U1, float Z0, float Z1)
		{
			const FVector Center = F.Normal * (F.Offset + Proud * 0.5f) + F.Along * ((U0 + U1) * 0.5f) + FVector(0, 0, GFloorTop + (Z0 + Z1) * 0.5f);
			const FVector Size = F.Along.GetAbs() * (U1 - U0) + F.Normal.GetAbs() * Proud + FVector(0, 0, Z1 - Z0);
			UStaticMeshComponent* Part = AddShape(this, Root, EShape::Cube, Center, Size, Palette::WoodDark, FRotator::ZeroRotator, false);
			Part->SetMaterial(0, Oak);
		};
		const float Rim = F.Half - GWallThickness;
		// Lisse basse et sablière haute, sur toute la façade (sauf la porte).
		if (F.GapHalf > 0.f && F.GapTop >= GDoorHeight)
		{
			Piece(-Rim, -F.GapHalf, 0.f, Beam);
			Piece(F.GapHalf, Rim, 0.f, Beam);
		}
		else
		{
			Piece(-Rim, Rim, 0.f, Beam);
		}
		Piece(-Rim, Rim, H - Beam, H);
		// Lisse intermédiaire, interrompue par l'ouverture.
		if (F.GapHalf > 0.f)
		{
			Piece(-Rim, -F.GapHalf, MidRail - Beam * 0.5f, MidRail + Beam * 0.5f);
			Piece(F.GapHalf, Rim, MidRail - Beam * 0.5f, MidRail + Beam * 0.5f);
		}
		else
		{
			Piece(-Rim, Rim, MidRail - Beam * 0.5f, MidRail + Beam * 0.5f);
		}
		// Poteaux à mi-chemin entre l'ouverture et l'angle, et croix dans les panneaux bas.
		const float Inner = FMath::Max(F.GapHalf, 0.f);
		const float Stud = (Inner + Rim) * 0.5f;
		for (const float S : { -1.f, 1.f })
		{
			Piece(S * Stud - Beam * 0.5f, S * Stud + Beam * 0.5f, Beam, H - Beam);
			const float A = S > 0 ? Stud + Beam * 0.5f : -Rim;
			const float B = S > 0 ? Rim : -Stud - Beam * 0.5f;
			const float Width = B - A;
			const float Height = MidRail - Beam * 1.5f;
			const float Length = FMath::Sqrt(Width * Width + Height * Height);
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Height, Width));
			for (const float Dir : { -1.f, 1.f })
			{
				const FVector Center = F.Normal * (F.Offset + Proud * 0.5f) + F.Along * ((A + B) * 0.5f) + FVector(0, 0, GFloorTop + Beam + Height * 0.5f);
				// Le long de la façade, incliné dans son plan : tangage si la façade suit X, roulis si elle suit Y.
				const FRotator Tilt = F.Along.X != 0.f ? FRotator(Dir * Angle, 0.f, 0.f) : FRotator(0.f, 0.f, -Dir * Angle);
				const FVector Size = F.Along.GetAbs() * Length + F.Normal.GetAbs() * Proud + FVector(0, 0, Beam * 0.8f);
				UStaticMeshComponent* Brace = AddShape(this, Root, EShape::Cube, Center, Size, Palette::WoodDark, Tilt, false);
				Brace->SetMaterial(0, Oak);
			}
		}
	}
}

void ASpawnCabin::BuildStoreInterior()
{
	using namespace ErewhonSpawn;
	const float HL = HalfLength;
	const float HW = HalfWidth;
	const float T = GWallThickness;

	// Comptoir entre l'entrée et le marchand.
	const FVector CounterCenter = AddCounter();

	// Accessoires générés autour de la porte et contre les murs.
	if (UStaticMesh* Barrel = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_barrel")))
	{
		AddGeneratedMesh(this, Root, Barrel, FVector(-HL - 150, HW - 20, 0), 95.f, 20.f);
		AddGeneratedMesh(this, Root, Barrel, FVector(HL - 80, -HW + 70, GFloorTop), 95.f, -35.f);
	}
	if (UStaticMesh* Crates = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_crates")))
	{
		AddGeneratedMesh(this, Root, Crates, FVector(-HL - 160, -HW + 10, 0), 110.f, -10.f);
		AddGeneratedMesh(this, Root, Crates, FVector(CounterCenter.X + 120, HW - 70, GFloorTop), 100.f, 90.f);
	}

	// La chatte de la boutique et ses endroits favoris (repère monde, lacet : 0 = vers la porte).
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AShopCat* Cat = GetWorld()->SpawnActor<AShopCat>(GetActorLocation(), FRotator::ZeroRotator, Params))
		{
			const FVector Origin = GetActorLocation();
			const auto Spot = [&Origin](const FVector& Local, float Yaw) { return FTransform(FRotator(0.f, Yaw, 0.f), Origin + Local); };
			Cat->SetSpots({
				Spot(FVector(CounterCenter.X, 120.f, GFloorTop + 105.f), -15.f),        // au bout du comptoir
				Spot(FVector(CounterCenter.X + 120.f, HW - 70.f, GFloorTop + 82.f), 30.f),  // sur les caisses de pommes
				Spot(FVector(HL - 150.f, -HW + 70.f, GFloorTop), 20.f),                // au pied du tonneau
				Spot(FVector(-HL + 90.f, HW - 70.f, GFloorTop), 200.f),               // près de la porte, l'œil sur la pièce
			});
		}
	}

	const float Back = HL - T - 20.f;
	AddShelf(FVector(Back, -150.f, GFloorTop), 0.f, 1, EShelfGoods::Pantry);
	AddShelf(FVector(Back, 150.f, GFloorTop), 0.f, 2, EShelfGoods::Pantry);
	AddShelf(FVector(-HL * 0.5f, HW - T - 20.f, GFloorTop), 90.f, 3, EShelfGoods::Pantry);
	AddShelf(FVector(-HL * 0.5f, -HW + T + 20.f, GFloorTop), -90.f, 4, EShelfGoods::Pantry);
}

FVector ASpawnCabin::AddCounter()
{
	const FVector CounterCenter = GetActorTransform().InverseTransformPosition(ErewhonSpawn::MerchantLocation(Kind)) - FVector(120, 0, 0);
	if (UStaticMesh* Counter = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_counter")))
	{
		// Modèle Meshy, grand côté en travers de la pièce, façade vers la porte.
		AddFitted(Counter, FitToLength(Counter, 300.f, 105.f), FVector(CounterCenter.X, 0.f, GFloorTop), CounterMeshYaw, true);
	}
	else
	{
		AddBox(Root, FVector(CounterCenter.X - 30, -150, GFloorTop), FVector(CounterCenter.X + 30, 150, GFloorTop + 100), Palette::WoodDark, TEXT("wood_trim"), 160.f);
		AddBox(Root, FVector(CounterCenter.X - 40, -160, GFloorTop + 100), FVector(CounterCenter.X + 40, 160, GFloorTop + 108), Palette::WoodLight, TEXT("wood_planks"), 220.f);
	}
	return CounterCenter;
}

void ASpawnCabin::AddShelf(const FVector& Location, float Yaw, int32 Variant, EShelfGoods Goods)
{
	// Étagère construite en planches, garnie selon le commerce.
	// Repère d'une étagère : façade vers −X, largeur sur Y, pied à Z = 0.
	USceneComponent* Frame = NewObject<USceneComponent>(this);
	Frame->SetupAttachment(Root);
	Frame->SetRelativeLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));
	Frame->RegisterComponent();
	AddInstanceComponent(Frame);

	constexpr float Width = 120.f, Depth = 36.f, Height = 190.f, Board = 4.f;
	const auto Plank = [this, Frame](const FVector& Center, const FVector& Size)
	{
		UStaticMeshComponent* Part = AddShape(this, Frame, EShape::Cube, Center, Size, Palette::WoodLight);
		Part->SetMaterial(0, MakePaintedMaterial(Part, TEXT("wood_planks"), 90.f, Palette::WoodLight, FLinearColor(0.85f, 0.75f, 0.62f)));
	};
	for (const float SideY : { -1.f, 1.f })
	{
		Plank(FVector(0.f, SideY * (Width * 0.5f - Board * 0.5f), Height * 0.5f), FVector(Depth, Board, Height));
	}
	Plank(FVector(Depth * 0.5f - 1.f, 0.f, Height * 0.5f), FVector(2.f, Width, Height));   // fond, côté mur
	const float Levels[] = { 6.f, 52.f, 98.f, 144.f, Height - Board * 0.5f };
	for (const float Z : Levels)
	{
		Plank(FVector(0.f, 0.f, Z), FVector(Depth, Width - 2 * Board, Board));
	}

	if (Goods == EShelfGoods::None)
	{
		return;   // garnie par d'autres (les potions de l'apothicairerie)
	}
	FRandomStream Rand(Variant * 7919 + static_cast<int32>(Goods) * 104729);
	const float Inner = Width - 2 * Board - 6.f;   // largeur utile d'un rayon
	const auto Prop = [this, Frame](EShape Shape, const FVector& Center, const FVector& Size, const FLinearColor& Color, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		return AddShape(this, Frame, Shape, Center, Size, Color, Rotation, false);
	};

	for (int32 Level = 0; Level < 4; ++Level)
	{
		const float Top = Levels[Level] + Board * 0.5f;
		if (Goods == EShelfGoods::Pantry)
		{
			// Sacs en bas, puis bocaux et bouteilles en alternance ; quelques trous.
			UStaticMesh* Jars = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_props_jars"));
			UStaticMesh* Bottles = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_props_bottles"));
			UStaticMesh* Sacks = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_props_sacks"));
			const uint32 Pick = HashCombine(GetTypeHash(Variant), GetTypeHash(Level));
			if (Pick % 5 == 0 && Level > 0)
			{
				continue;   // étagère à demi vide, plus naturel
			}
			UStaticMesh* Mesh = Level == 0 ? Sacks : (Pick % 2 == 0 ? Jars : Bottles);
			if (!Mesh)
			{
				continue;
			}
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
			Part->SetStaticMesh(Mesh);
			Part->SetupAttachment(Frame);
			Part->SetRelativeTransform(FitToLength(Mesh, Width - 20.f, Level == 0 ? 40.f : 34.f) * FTransform(FVector(-2.f, 0.f, Top)));
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->RegisterComponent();
			AddInstanceComponent(Part);
		}
		else if (Goods == EShelfGoods::Office)
		{
			// Bureau du gérant : rouleaux de plans, livres reliés, cartes pliées, instruments de laiton.
			const int32 Arrangement = (Level + Variant) % 4;
			if (Arrangement == 0)
			{
				// Rouleaux empilés en pyramide, nœud de ruban rouge au milieu.
				int32 Index = 0;
				for (int32 Row = 0; Row < 3; ++Row)
				{
					for (int32 i = 0; i < 4 - Row; ++i)
					{
						const float Y = -Inner * 0.5f + 18.f + (i + Row * 0.5f) * 19.f + Rand.FRandRange(-1.f, 1.f);
						const FVector Center(-2.f, Y, Top + 4.5f + Row * 8.f);
						Prop(EShape::Cylinder, Center, FVector(9.f, 9.f, 30.f), GParchment * Rand.FRandRange(0.85f, 1.05f), FRotator(90.f, 0.f, 0.f));
						if (++Index % 2 == 0)
						{
							Prop(EShape::Cylinder, Center, FVector(9.6f, 9.6f, 3.f), FLinearColor(0.55f, 0.08f, 0.06f), FRotator(90.f, 0.f, 0.f));
						}
					}
				}
				// Une longue-vue en laiton couchée à côté.
				Prop(EShape::Cylinder, FVector(-6.f, Inner * 0.5f - 14.f, Top + 3.5f), FVector(6.f, 6.f, 34.f), GBrass, FRotator(90.f, 0.f, 0.f));
			}
			else if (Arrangement == 1 || Arrangement == 3)
			{
				// Une rangée de livres reliés, de hauteurs variées, le dernier penché.
				const FLinearColor Leathers[] = {
					FLinearColor::FromSRGBColor(FColor(110, 40, 32)), FLinearColor::FromSRGBColor(FColor(46, 70, 52)),
					FLinearColor::FromSRGBColor(FColor(52, 60, 96)), FLinearColor::FromSRGBColor(FColor(120, 86, 44)),
					FLinearColor::FromSRGBColor(FColor(86, 52, 80)),
				};
				float Y = -Inner * 0.5f + 2.f;
				const float End = Arrangement == 1 ? Inner * 0.5f - 4.f : Inner * 0.15f;
				while (Y < End - 6.f)
				{
					const float Thick = Rand.FRandRange(3.f, 6.5f);
					const float Tall = Rand.FRandRange(24.f, 34.f);
					Prop(EShape::Cube, FVector(-2.f, Y + Thick * 0.5f, Top + Tall * 0.5f), FVector(22.f, Thick, Tall), Leathers[Rand.RandHelper(5)]);
					Y += Thick + 0.4f;
				}
				Prop(EShape::Cube, FVector(-2.f, Y + 9.f, Top + 13.f), FVector(22.f, 5.f, 28.f), Leathers[Rand.RandHelper(5)], FRotator(0.f, 0.f, -24.f));
				if (Arrangement == 3)
				{
					// Un compas de marine en laiton, posé à plat.
					Prop(EShape::Cylinder, FVector(-4.f, Inner * 0.32f, Top + 1.5f), FVector(16.f, 16.f, 3.f), GBrass);
					Prop(EShape::Cylinder, FVector(-4.f, Inner * 0.32f, Top + 3.2f), FVector(13.f, 13.f, 0.6f), GParchment);
				}
			}
			else
			{
				// Cartes pliées en piles, et un petit globe sur son pied.
				for (int32 Stack = 0; Stack < 2; ++Stack)
				{
					const float Y = -Inner * 0.5f + 20.f + Stack * 34.f;
					const int32 Count = Rand.RandRange(3, 6);
					for (int32 i = 0; i < Count; ++i)
					{
						Prop(EShape::Cube, FVector(-3.f + Rand.FRandRange(-1.f, 1.f), Y + Rand.FRandRange(-1.5f, 1.5f), Top + 1.f + i * 2.f),
							FVector(24.f, 30.f, 1.8f), GParchment * Rand.FRandRange(0.82f, 1.05f), FRotator(0.f, Rand.FRandRange(-6.f, 6.f), 0.f));
					}
				}
				const FVector Globe(-4.f, Inner * 0.5f - 22.f, Top);
				Prop(EShape::Cylinder, Globe + FVector(0, 0, 1.f), FVector(12.f, 12.f, 2.f), Palette::WoodDark);
				Prop(EShape::Cylinder, Globe + FVector(0, 0, 8.f), FVector(1.6f, 1.6f, 14.f), GBrass);
				UStaticMeshComponent* Ball = Prop(EShape::Sphere, Globe + FVector(0, 0, 25.f), FVector(22.f), FLinearColor(0.35f, 0.55f, 0.6f), FRotator(0.f, 0.f, 23.f));
				if (UTexture* Map = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_map_wall")))
				{
					if (UMaterialInterface* Master = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_GeneratedMesh")))
					{
						UMaterialInstanceDynamic* Painted = UMaterialInstanceDynamic::Create(Master, Ball);
						Painted->SetTextureParameterValue(TEXT("BaseColor"), Map);
						Ball->SetMaterial(0, Painted);
					}
				}
			}
		}
		else
		{
			// Mercerie : rouleaux d'étoffe couchés côte à côte, piles de tissus pliés en bas.
			const FLinearColor Fabrics[] = {
				FLinearColor::FromSRGBColor(FColor(196, 92, 72)), FLinearColor::FromSRGBColor(FColor(82, 128, 160)),
				FLinearColor::FromSRGBColor(FColor(222, 196, 120)), FLinearColor::FromSRGBColor(FColor(110, 150, 96)),
				FLinearColor::FromSRGBColor(FColor(232, 222, 206)), FLinearColor::FromSRGBColor(FColor(150, 96, 140)),
				FLinearColor::FromSRGBColor(FColor(70, 70, 96)),
			};
			if (Level == 0)
			{
				for (int32 Stack = 0; Stack < 3; ++Stack)
				{
					const int32 Count = Rand.RandRange(3, 5);
					for (int32 i = 0; i < Count; ++i)
					{
						Prop(EShape::Cube, FVector(-3.f, -Inner * 0.5f + 18.f + Stack * 36.f, Top + 2.5f + i * 5.f),
							FVector(26.f, 30.f, 4.6f), Fabrics[Rand.RandHelper(7)], FRotator(0.f, Rand.FRandRange(-4.f, 4.f), 0.f));
					}
				}
				continue;
			}
			const int32 Count = 6;
			for (int32 i = 0; i < Count; ++i)
			{
				const float Y = -Inner * 0.5f + 9.5f + i * (Inner - 19.f) / (Count - 1);
				Prop(EShape::Cylinder, FVector(-2.f, Y, Top + 9.f), FVector(17.f, 17.f, 32.f), Fabrics[Rand.RandHelper(7)], FRotator(90.f, 0.f, 0.f));
			}
		}
	}
}

void ASpawnCabin::BuildManagerInterior()
{
	using namespace ErewhonSpawn;
	const float HL = HalfLength;
	const float HW = HalfWidth;
	const float T = GWallThickness;
	const FVector Merchant = GetActorTransform().InverseTransformPosition(MerchantLocation(Kind));

	// Le bureau aux cartes, entre l'entrée et le gérant.
	const FVector DeskCenter(Merchant.X - 105.f, 0.f, GFloorTop);
	if (UStaticMesh* Desk = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_desk_maps")))
	{
		AddFitted(Desk, FitToLength(Desk, 190.f, 95.f), DeskCenter, DeskMeshYaw, true);
	}
	else
	{
		AddBox(Root, DeskCenter + FVector(-40, -90, 76), DeskCenter + FVector(40, 90, 84), Palette::WoodLight, TEXT("wood_planks"), 120.f);
		for (const float SY : { -1.f, 1.f })
		{
			AddShape(this, Root, EShape::Cube, DeskCenter + FVector(0, SY * 80.f, 38), FVector(70, 10, 76), Palette::WoodDark);
		}
	}

	// La carte du pays, encadrée au mur du fond, derrière le gérant.
	const FVector MapCenter(HL - T - 4.f, 0.f, GFloorTop + 170.f);
	if (AddPicture(this, Root, MapCenter, 190.f, 142.f, 0.f, TEXT("T_map_wall")))
	{
		AddBox(Root, MapCenter + FVector(1.f, -102, -78), MapCenter + FVector(4.f, 102, 78), Palette::WoodDark, TEXT("wood_trim"), 140.f, false);
	}

	// Une étagère de chaque côté (rouleaux, livres, cartes, instruments), un tonneau dehors, des caisses au fond.
	AddShelf(FVector(-HL * 0.5f, HW - T - 20.f, GFloorTop), 90.f, 5, EShelfGoods::Office);
	AddShelf(FVector(HL * 0.5f, -HW + T + 20.f, GFloorTop), -90.f, 6, EShelfGoods::Office);
	if (UStaticMesh* Barrel = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_barrel")))
	{
		AddGeneratedMesh(this, Root, Barrel, FVector(-HL - 150, -HW + 30, 0), 95.f, 50.f);
	}
	if (UStaticMesh* Crates = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_crates")))
	{
		AddGeneratedMesh(this, Root, Crates, FVector(HL - 90, HW - 80, GFloorTop), 90.f, 15.f);
	}
}

void ASpawnCabin::BuildTailorInterior()
{
	using namespace ErewhonSpawn;
	const float HL = HalfLength;
	const float HW = HalfWidth;
	const float T = GWallThickness;

	const FVector CounterCenter = AddCounter();
	// Pelotes de laine dans une corbeille, sur le comptoir.
	{
		const FVector Basket(CounterCenter.X, -95.f, GFloorTop + 105.f);
		AddShape(this, Root, EShape::Cylinder, Basket + FVector(0, 0, 6), FVector(34, 34, 12), FLinearColor::FromSRGBColor(FColor(176, 136, 84)), FRotator::ZeroRotator, false);
		const FLinearColor Yarns[] = { FLinearColor::FromSRGBColor(FColor(200, 70, 60)), FLinearColor::FromSRGBColor(FColor(80, 130, 180)), FLinearColor::FromSRGBColor(FColor(230, 200, 90)) };
		for (int32 i = 0; i < 3; ++i)
		{
			const float Angle = i * UE_TWO_PI / 3.f;
			AddShape(this, Root, EShape::Sphere, Basket + FVector(FMath::Cos(Angle) * 7.f, FMath::Sin(Angle) * 7.f, 15.f), FVector(13.f), Yarns[i], FRotator::ZeroRotator, false);
		}
	}

	// Rouleaux d'étoffe au mur du fond.
	const float Back = HL - T - 20.f;
	AddShelf(FVector(Back, -150.f, GFloorTop), 0.f, 7, EShelfGoods::Fabric);
	AddShelf(FVector(Back, 150.f, GFloorTop), 0.f, 8, EShelfGoods::Fabric);

	// Un mannequin de couturière près de l'entrée, une robe en cours sur le buste.
	{
		const FVector Foot(-HL * 0.45f, HW - 80.f, GFloorTop);
		const FLinearColor Dress = FLinearColor::FromSRGBColor(FColor(110, 150, 120));
		AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, 2), FVector(40, 40, 4), Palette::WoodDark, FRotator::ZeroRotator, false);
		AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, 50), FVector(4, 4, 96), Palette::WoodDark, FRotator::ZeroRotator, false);
		AddShape(this, Root, EShape::Cone, Foot + FVector(0, 0, 72), FVector(56, 56, 52), Dress, FRotator::ZeroRotator, false);
		AddShape(this, Root, EShape::Sphere, Foot + FVector(0, 0, 128), FVector(36, 28, 50), Dress, FRotator::ZeroRotator, false);
		AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, 156), FVector(8, 8, 10), FLinearColor::FromSRGBColor(FColor(222, 206, 178)), FRotator::ZeroRotator, false);
	}

	// Un portant de vêtements contre l'autre mur : chemises et vestes aux couleurs de la boutique.
	{
		const float X0 = -HL * 0.6f, X1 = HL * 0.15f;
		const float Y = -HW + T + 40.f;
		for (const float X : { X0, X1 })
		{
			AddShape(this, Root, EShape::Cylinder, FVector(X, Y, GFloorTop + 85.f), FVector(5, 5, 170), Palette::WoodDark, FRotator::ZeroRotator, true);
			AddShape(this, Root, EShape::Cube, FVector(X, Y, GFloorTop + 2.f), FVector(8, 46, 4), Palette::WoodDark, FRotator::ZeroRotator, false);
		}
		AddShape(this, Root, EShape::Cylinder, FVector((X0 + X1) * 0.5f, Y, GFloorTop + 166.f), FVector(3, 3, X1 - X0), GBrass, FRotator(90.f, 0.f, 0.f), false);
		const FLinearColor Clothes[] = {
			FLinearColor::FromSRGBColor(FColor(230, 120, 40)), FLinearColor::FromSRGBColor(FColor(50, 100, 200)),
			FLinearColor::FromSRGBColor(FColor(30, 60, 130)), FLinearColor::FromSRGBColor(FColor(100, 60, 30)),
			FLinearColor::FromSRGBColor(FColor(200, 40, 40)), FLinearColor::FromSRGBColor(FColor(232, 222, 206)),
		};
		for (int32 i = 0; i < 6; ++i)
		{
			const float X = X0 + 22.f + i * (X1 - X0 - 44.f) / 5.f;
			AddShape(this, Root, EShape::Cube, FVector(X, Y, GFloorTop + 128.f), FVector(4, 44, 60), Clothes[i], FRotator(0.f, 0.f, 0.f), false);
			AddShape(this, Root, EShape::Cube, FVector(X, Y, GFloorTop + 160.f), FVector(1.5f, 36, 2), Palette::WoodDark, FRotator::ZeroRotator, false);
		}
	}
}

void ASpawnCabin::BuildApothecaryInterior()
{
	using namespace ErewhonSpawn;
	const float HL = HalfLength;
	const float HW = HalfWidth;
	const float H = WallHeight;
	const float T = GWallThickness;
	const auto Stone = [this](const FVector& Min, const FVector& Max, const FLinearColor& Tint = FLinearColor::White)
	{
		const FVector Floor(0, 0, GFloorTop);
		UStaticMeshComponent* Part = AddBox(Root, Min + Floor, Max + Floor, Palette::Stone, TEXT("stone"), 110.f);
		Part->SetMaterial(0, MakePaintedMaterial(Part, TEXT("stone"), 110.f, Palette::Stone, Tint));
		return Part;
	};

	// La cheminée, contre le mur du fond : le modèle peint, ou à défaut des blocs de pierre.
	const float Back = HL - T;
	if (UStaticMesh* Fireplace = ErewhonApothecary::HasFireplaceModel() ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_fireplace")) : nullptr)
	{
		// Mise à la taille : large de FireplaceWidth, moins profonde que le modèle ; le haut traverse le plafond.
		const FBox Bounds = Fireplace->GetBoundingBox();
		const FVector Size = Bounds.GetSize();
		const bool bWideX = Size.X > Size.Y;
		const float S = ErewhonApothecary::FireplaceWidth / (bWideX ? Size.X : Size.Y);
		const float SD = ErewhonApothecary::FireplaceDepth / (bWideX ? Size.Y : Size.X);
		const FVector Scale = bWideX ? FVector(S, SD, S) : FVector(SD, S, S);
		const FVector Center = Bounds.GetCenter();
		const FTransform Fit(FRotator::ZeroRotator, -FVector(Center.X * Scale.X, Center.Y * Scale.Y, Bounds.Min.Z * Scale.Z), Scale);
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetStaticMesh(Fireplace);
		Part->SetupAttachment(Root);
		Part->SetRelativeTransform(Fit * FTransform(FRotator(0.f, (bWideX ? 90.f : 0.f) + FireplaceFrontYaw, 0.f), FVector(Back - ErewhonApothecary::FireplaceDepth * 0.5f, 0.f, GFloorTop)));
		// Elle arrête le joueur, mais pas la visée : on vise les bûches et le chaudron au fond du foyer.
		Part->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Part->RegisterComponent();
		AddInstanceComponent(Part);
	}
	else
	{
		Stone(FVector(Back - 100.f, -115.f, 0.f), FVector(Back, 115.f, 8.f));
		for (const float SY : { -1.f, 1.f })
		{
			Stone(FVector(Back - 80.f, SY > 0 ? 62.f : -102.f, 0.f), FVector(Back, SY > 0 ? 102.f : -62.f, 125.f));
		}
		Stone(FVector(Back - 80.f, -102.f, 125.f), FVector(Back, 102.f, 152.f));
		AddBox(Root, FVector(Back - 92.f, -118.f, GFloorTop + 152.f), FVector(Back, 118.f, GFloorTop + 162.f), Palette::WoodDark, TEXT("wood_trim"), 140.f);
		Stone(FVector(Back - 52.f, -84.f, 162.f), FVector(Back, 84.f, H - 10.f));
		// Le fond du foyer, noirci de suie.
		Stone(FVector(Back - 14.f, -62.f, 8.f), FVector(Back, 62.f, 125.f), FLinearColor(0.18f, 0.16f, 0.15f));
		// Sur le manteau : deux pots et une chandelle.
		AddShape(this, Root, EShape::Cylinder, FVector(Back - 40.f, -70.f, GFloorTop + 172.f), FVector(14, 14, 20), FLinearColor::FromSRGBColor(FColor(150, 90, 60)), FRotator::ZeroRotator, false);
		AddShape(this, Root, EShape::Cylinder, FVector(Back - 40.f, 75.f, GFloorTop + 170.f), FVector(12, 12, 16), FLinearColor::FromSRGBColor(FColor(90, 120, 150)), FRotator::ZeroRotator, false);
		AddShape(this, Root, EShape::Cylinder, FVector(Back - 40.f, 20.f, GFloorTop + 172.f), FVector(5, 5, 20), FLinearColor::FromSRGBColor(FColor(240, 230, 200)), FRotator::ZeroRotator, false);
	}

	// La souche de cheminée qui traverse le toit, au-dessus du pignon du fond.
	{
		const FCabinStyle Style = StyleFor(Kind);
		const float RidgeRise = (HW + GRoofOverhang) * FMath::Tan(FMath::DegreesToRadians(Style.RoofSlope));
		Stone(FVector(HL - 70.f, -42.f, H - 10.f), FVector(HL + 6.f, 42.f, H + RidgeRise + 70.f));
		Stone(FVector(HL - 76.f, -48.f, H + RidgeRise + 70.f), FVector(HL + 12.f, 48.f, H + RidgeRise + 82.f));
	}

	// Le mobilier (table, mortier, chaudron, étagères) s'achète : la maison est livrée vide.

	// Des bouquets de plantes qui sèchent, pendus aux poutres, tête en bas.
	{
		const FLinearColor Herbs[] = {
			FLinearColor::FromSRGBColor(FColor(110, 150, 70)), FLinearColor::FromSRGBColor(FColor(150, 110, 170)),
			FLinearColor::FromSRGBColor(FColor(90, 130, 60)), FLinearColor::FromSRGBColor(FColor(190, 160, 80)),
			FLinearColor::FromSRGBColor(FColor(80, 120, 160)),
		};
		FRandomStream Rand(4242);
		int32 Index = 0;
		for (const float X : { -HL * 0.5f, 0.f })
		{
			for (float Y = -HW + 70.f; Y < HW - 60.f; Y += Rand.FRandRange(55.f, 85.f))
			{
				const float Drop = Rand.FRandRange(18.f, 34.f);
				const float Top = GFloorTop + H - 32.f;
				AddShape(this, Root, EShape::Cylinder, FVector(X, Y, Top - Drop * 0.5f), FVector(1.2f, 1.2f, Drop), Palette::WoodDark, FRotator::ZeroRotator, false);
				AddShape(this, Root, EShape::Sphere, FVector(X, Y, Top - Drop - 14.f), FVector(16, 16, 32), Herbs[Index++ % UE_ARRAY_COUNT(Herbs)], FRotator::ZeroRotator, false);
			}
		}
	}

	// Un panier de bûches près de l'âtre.
	{
		const FVector Basket(Back - 70.f, -160.f, GFloorTop);
		AddShape(this, Root, EShape::Cylinder, Basket + FVector(0, 0, 18), FVector(56, 56, 36), FLinearColor::FromSRGBColor(FColor(176, 136, 84)), FRotator::ZeroRotator, true);
		for (int32 i = 0; i < 4; ++i)
		{
			AddShape(this, Root, EShape::Cylinder, Basket + FVector(-12.f + i * 8.f, 0, 40.f + (i % 2) * 6.f), FVector(10, 10, 54),
				FLinearColor::FromSRGBColor(FColor(110, 80, 52)), FRotator(90.f, 10.f + i * 25.f, 0.f), false);
		}
	}
}

// ── L'éleveur ────────────────────────────────────────────────────────────────

void ASpawnCabin::BuildBreederInterior()
{
	const float HL = HalfLength;
	const float HW = HalfWidth;
	const float T = GWallThickness;
	AddCounter();
	// Le harnachement en montre : une selle sur un chevalet, des sacoches au mur ; du foin dans un coin.
	const FVector Rack(-HL * 0.35f, HW - T - 70.f, GFloorTop);
	AddBox(Root, Rack + FVector(-60.f, -12.f, 0.f), Rack + FVector(60.f, 12.f, 80.f), Palette::WoodDark, TEXT("wood_trim"), 140.f);
	if (UStaticMesh* Saddle = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_saddle")))
	{
		AddGeneratedMesh(this, Root, Saddle, Rack + FVector(0.f, 0.f, 70.f), 45.f, 90.f, false);
	}
	if (UStaticMesh* Bag = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_saddlebag")))
	{
		for (const float X : { HL * 0.1f, HL * 0.35f })
		{
			AddGeneratedMesh(this, Root, Bag, FVector(X, HW - T - 22.f, GFloorTop + 120.f), 50.f, 180.f, false);
		}
	}
	if (UStaticMesh* Hay = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_hay_bale")))
	{
		AddGeneratedMesh(this, Root, Hay, FVector(HL - 110.f, -HW + 100.f, GFloorTop), 55.f, 10.f);
		AddGeneratedMesh(this, Root, Hay, FVector(HL - 110.f, -HW + 100.f, GFloorTop + 55.f), 55.f, -15.f);
		AddGeneratedMesh(this, Root, Hay, FVector(-HL - 170.f, HW - 40.f, 0.f), 55.f, 30.f);
	}
	AddShelf(FVector(-HL * 0.4f, -HW + T + 20.f, GFloorTop), -90.f, 7, EShelfGoods::Pantry);
}

void ASpawnCabin::BuildBarn()
{
	// La grange, à côté de la maison : ouverte sur le devant (−X), trois box, un bhurak de chaque robe.
	const FVector2D C = ErewhonSpawn::AnnexCenter(Kind);
	const FVector2D Half = ErewhonSpawn::AnnexHalfSize(Kind);
	constexpr float H = 380.f;
	constexpr float T = 20.f;
	const FLinearColor Red = FLinearColor::FromSRGBColor(FColor(160, 70, 52));
	const auto Wall = [this, &Red](const FVector& Min, const FVector& Max)
	{
		return AddBox(Root, Min, Max, Red, TEXT("barn_red"), 300.f);
	};
	const auto Timber = [this](const FVector& Min, const FVector& Max, bool bCollision = true)
	{
		return AddBox(Root, Min, Max, Palette::WoodDark, TEXT("wood_trim"), 140.f, bCollision);
	};
	const float X0 = C.X - Half.X, X1 = C.X + Half.X;
	const float Y0 = C.Y - Half.Y, Y1 = C.Y + Half.Y;
	// Sol de paille, murs du fond et des côtés, poteaux de façade.
	AddBox(Root, FVector(X0, Y0, -10.f), FVector(X1, Y1, 3.f), Palette::WoodLight, TEXT("straw_floor"), 260.f);
	Wall(FVector(X1 - T, Y0, 0.f), FVector(X1, Y1, H));
	Wall(FVector(X0, Y0, 0.f), FVector(X1, Y0 + T, H));
	Wall(FVector(X0, Y1 - T, 0.f), FVector(X1, Y1, H));
	for (int32 i = 0; i <= 3; ++i)
	{
		const float Y = FMath::Lerp(Y0 + 15.f, Y1 - 15.f, i / 3.f);
		Timber(FVector(X0, Y - 15.f, 0.f), FVector(X0 + 30.f, Y + 15.f, H));
	}
	Timber(FVector(X0, Y0, H - 30.f), FVector(X0 + 30.f, Y1, H), false);
	// Toit à deux pans de chaume, faîtage dans l'axe X ; pignon plein au fond, pignon au-dessus de l'ouverture.
	constexpr float Slope = 40.f;
	constexpr float Overhang = 60.f;
	const float HalfSpan = Half.Y + Overhang;
	const float SlabWidth = HalfSpan / FMath::Cos(FMath::DegreesToRadians(Slope));
	const float Rise = HalfSpan * FMath::Tan(FMath::DegreesToRadians(Slope));
	for (const float Side : { -1.f, 1.f })
	{
		UStaticMeshComponent* Slab = AddShape(this, Root, EShape::Cube, FVector(C.X, C.Y + Side * HalfSpan * 0.5f, H + Rise * 0.5f + 8.f),
			FVector(2.f * Half.X + 2.f * Overhang, SlabWidth, 22.f), Palette::Roof, FRotator(0.f, 0.f, Side * Slope));
		Slab->SetMaterial(0, MakePaintedMaterial(Slab, TEXT("thatch"), 300.f, FLinearColor::FromSRGBColor(FColor(214, 178, 96))));
	}
	UMaterialInterface* Gable = MakePaintedMaterial(this, TEXT("barn_red"), 300.f, Red);
	const float EaveRise = Rise * Overhang / HalfSpan;
	const TArray<FVector2D> GableShape = { FVector2D(Y0, H), FVector2D(Y1, H), FVector2D(Y1, H + EaveRise), FVector2D(C.Y, H + Rise - 4.f), FVector2D(Y0, H + EaveRise) };
	AddPrism(this, Root, GableShape, X1 - T, X1, Gable);
	AddPrism(this, Root, GableShape, X0, X0 + T, Gable);

	// Trois box : des cloisons à mi-hauteur, une lisse devant chacun.
	const float StallWidth = (Y1 - Y0) / 3.f;
	for (int32 i = 1; i < 3; ++i)
	{
		const float Y = Y0 + StallWidth * i;
		AddBox(Root, FVector(X0 + 120.f, Y - 6.f, 0.f), FVector(X1 - T, Y + 6.f, 150.f), Palette::WoodLight, TEXT("wood_planks"), 160.f);
	}
	for (int32 i = 0; i < 3; ++i)
	{
		const float Ya = Y0 + StallWidth * i + 30.f, Yb = Y0 + StallWidth * (i + 1) - 30.f;
		for (const float Z : { 60.f, 110.f })
		{
			Timber(FVector(X0 + 110.f, Ya, Z), FVector(X0 + 124.f, Yb, Z + 12.f));
		}
		// Le bhurak du box, tourné vers l'allée, et une botte de foin au fond.
		const FVector Stall(C.X + 60.f, Y0 + StallWidth * (i + 0.5f), 0.f);
		FTransform Fit;
		if (UStaticMesh* Beast = ABhurak::Mesh(Fit, static_cast<uint8>(i)))
		{
			AddFitted(Beast, Fit, Stall, 180.f, false);
		}
		if (UStaticMesh* Hay = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_hay_bale")))
		{
			AddGeneratedMesh(this, Root, Hay, FVector(X1 - 90.f, Stall.Y + 60.f, 0.f), 50.f, 90.f + 20.f * i, false);
		}
	}
	// Une lanterne sous la panne, au milieu.
	UPointLightComponent* BarnLamp = NewObject<UPointLightComponent>(this);
	BarnLamp->SetupAttachment(Root);
	BarnLamp->SetRelativeLocation(FVector(C.X, C.Y, H - 40.f));
	BarnLamp->SetIntensityUnits(ELightUnits::Lumens);
	BarnLamp->SetIntensity(1200.f);
	BarnLamp->SetAttenuationRadius(1100.f);
	BarnLamp->SetLightColor(FLinearColor::FromSRGBColor(FColor(255, 196, 120)));
	BarnLamp->RegisterComponent();
	AddInstanceComponent(BarnLamp);
}

// ── L'atelier ────────────────────────────────────────────────────────────────

void ASpawnCabin::BuildWorkshopInterior()
{
	const float HL = HalfLength;
	const float HW = HalfWidth;
	const float T = GWallThickness;
	AddCounter();
	// L'enclume près du fond, un établi au mur, du bois débité, des barriques.
	if (UStaticMesh* Anvil = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_anvil")))
	{
		AddGeneratedMesh(this, Root, Anvil, FVector(HL * 0.4f, -HW + 110.f, GFloorTop), 85.f, 20.f);
	}
	if (UStaticMesh* Bench = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_workbench")))
	{
		AddFitted(Bench, FitToBox(Bench, 200.f, 80.f, 110.f), FVector(-HL * 0.25f, HW - T - 50.f, GFloorTop), 0.f, true);
	}
	if (UStaticMesh* Lumber = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_lumber_pile")))
	{
		AddFitted(Lumber, FitToLength(Lumber, 260.f, 60.f), FVector(-HL * 0.45f, -HW + 80.f, GFloorTop), 90.f, true);
	}
	if (UStaticMesh* Barrel = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_barrel")))
	{
		AddGeneratedMesh(this, Root, Barrel, FVector(-HL - 150, -HW + 40, 0), 95.f, 25.f);
	}
}

void ASpawnCabin::BuildYard()
{
	// La cour, à côté de l'atelier : un enclos de barrières ouvert vers la route (−X), et ce qui s'y vend, exposé.
	const FVector2D C = ErewhonSpawn::AnnexCenter(Kind);
	const FVector2D Half = ErewhonSpawn::AnnexHalfSize(Kind);
	UStaticMesh* Fence = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_fence_segment"));
	const auto FenceRun = [&](const FVector2D& A, const FVector2D& B)
	{
		const float Length = FVector2D::Distance(A, B);
		const int32 Count = FMath::Max(1, FMath::RoundToInt32(Length / 250.f));
		const FVector2D Dir = (B - A) / Length;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector2D Mid = A + Dir * (Length * (i + 0.5f) / Count);
			if (Fence)
			{
				AddFitted(Fence, FitToLength(Fence, Length / Count, 110.f) * FTransform(FRotator(0.f, -90.f, 0.f)), FVector(Mid, 0.f), Yaw, true);
			}
			else
			{
				AddShape(this, Root, EShape::Cube, FVector(Mid, 80.f), FVector(Length / Count, 8.f, 10.f), Palette::WoodDark, FRotator(0.f, Yaw, 0.f));
			}
		}
	};
	const FVector2D A(C.X - Half.X, C.Y - Half.Y), B(C.X + Half.X, C.Y - Half.Y), D(C.X + Half.X, C.Y + Half.Y), E(C.X - Half.X, C.Y + Half.Y);
	FenceRun(B, D);   // le fond
	FenceRun(D, E);   // le côté loin de la maison
	// Côté maison : on laisse un passage vers la porte de côté.
	FenceRun(A + FVector2D(Half.X * 0.6f, 0.f), B);
	FenceRun(E, E + FVector2D(Half.X * 0.35f, 0.f));
	// Ce qui s'y vend : la charrette, le cultivateur, le semoir, alignés face à la route.
	const ECartKind Shown[] = { ECartKind::Cart, ECartKind::Cultivator, ECartKind::Seeder };
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector Spot(C.X + 80.f, C.Y - Half.Y * 0.62f + Half.Y * 0.62f * i, 0.f);
		FTransform Fit;
		if (UStaticMesh* Body = ErewhonPlaceables::ImplementMesh(Shown[i], Fit))
		{
			AddFitted(Body, Fit, Spot, 0.f, true);   // brancards vers la route (−X)
		}
		if (Shown[i] == ECartKind::Cart)
		{
			// La charrette a ses roues à part.
			if (UStaticMesh* WheelMesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cart_wheel")))
			{
				const FVector Size = WheelMesh->GetBoundingBox().GetSize();
				const float DiskYaw = Size.X < Size.Y ? 90.f : 0.f;
				for (const float Side : { -1.f, 1.f })
				{
					const FVector Axle = FTransform(Spot).TransformPosition(FVector(ACart::AxleX, Side * ACart::WheelHalfTrack, 0.f));
					AddFitted(WheelMesh, FitToHeight(WheelMesh, ACart::WheelDiameter), Axle, DiskYaw, false);
				}
			}
		}
	}
	if (UStaticMesh* Lumber = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_lumber_pile")))
	{
		AddFitted(Lumber, FitToLength(Lumber, 300.f, 70.f), FVector(C.X + Half.X - 120.f, C.Y + Half.Y - 150.f, 0.f), 0.f, true);
	}
	if (UStaticMesh* Anvil = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_anvil")))
	{
		AddGeneratedMesh(this, Root, Anvil, FVector(C.X + Half.X - 120.f, C.Y - Half.Y + 120.f, 0.f), 85.f, -30.f);
	}
}

#undef LOCTEXT_NAMESPACE
