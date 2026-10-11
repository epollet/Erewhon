#include "World/SpawnCabin.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/ErewhonPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "World/ErewhonVisuals.h"
#include "World/ShopCat.h"
#include "World/SectorTypes.h"
#include "World/SpawnLayout.h"

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
	constexpr float GRoofSlope = 30.f;    // degrés
	constexpr float GRoofOverhang = 50.f;
}

// ── Marchand ─────────────────────────────────────────────────────────────────

AMerchant::AMerchant()
{
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(40.f, 90.f);
	Capsule->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RootComponent = Capsule;
}

void AMerchant::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_merchant")))
	{
		// Modèle Meshy, posé sur le plancher (bas de la capsule).
		AddGeneratedMesh(this, Capsule, Mesh, FVector(0, 0, -Capsule->GetScaledCapsuleHalfHeight()), 175.f, MerchantMeshYaw, false);
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
	return LOCTEXT("TalkToMerchant", "Parler au marchand");
}

void AMerchant::Interact(AErewhonPlayerController* Controller)
{
	if (Controller)
	{
		Controller->OpenStore();
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
	SetActorLocation(ErewhonSector::Center(FIntPoint::ZeroValue));
	BuildCabin();

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector MerchantLocation = ErewhonSector::MerchantLocation() + FVector(0, 0, GFloorTop + 90.f);
	// Le marchand fait face à la porte (sud, −X).
	GetWorld()->SpawnActor<AMerchant>(MerchantLocation, FRotator(0, 180, 0), Params);
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

void ASpawnCabin::BuildCabin()
{
	using namespace ErewhonSpawn;
	const float HL = CabinHalfLength;
	const float HW = CabinHalfWidth;
	const float H = CabinWallHeight;
	const float T = GWallThickness;

	// Matériaux peints générés, avec la couleur unie en repli.
	const auto Paint = [](UStaticMeshComponent* Part, const FLinearColor& Color)
	{
		const TCHAR* Texture = TEXT("wood_planks");
		float Tile = 220.f;
		FLinearColor Tint = FLinearColor::White;
		if (Color == Palette::Stone)
		{
			Texture = TEXT("stone");
			Tile = 160.f;
		}
		else if (Color == Palette::Roof)
		{
			Texture = TEXT("roof_tiles");
			Tile = 240.f;
		}
		else if (Color == Palette::WoodDark)
		{
			Texture = TEXT("wood_trim");
			Tile = 160.f;
		}
		Part->SetMaterial(0, MakePaintedMaterial(Part, Texture, Tile, Color, Tint));
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
		AddLoft(this, Root, Bottom, -20.f, Top, GFloorTop, MakePaintedMaterial(this, TEXT("stone"), 160.f, Palette::Stone));
	}

	// Sol intérieur en dalles de pierre.
	UStaticMeshComponent* Floor = AddShape(this, Root, EShape::Cube, FVector(0, 0, GFloorTop + 1.f), FVector(2 * (HL - T), 2 * (HW - T), 2.f), Palette::Stone);
	Floor->SetMaterial(0, MakePaintedMaterial(Floor, TEXT("stone_slabs"), 220.f, Palette::Stone));

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
		Wood(FVector(Landing, -StairHalfWidth, -25.f), FVector(-HL + T, StairHalfWidth, GFloorTop + 1.f), false);
		for (int32 Step = 1; Step <= 2; ++Step)
		{
			const float Top = GFloorTop * (3 - Step) / 3.f;
			const float X1 = Landing - (Step - 1) * Tread;
			Wood(FVector(X1 - Tread, -StairHalfWidth, -10.f), FVector(X1, StairHalfWidth, Top), false);
		}
		for (const float Side : { -1.f, 1.f })
		{
			const float Y = Side * (StairHalfWidth + 5.f);
			Wood(FVector(Landing - 2 * Tread - 4.f, Y - 5.f, -25.f), FVector(-HL + T, Y + 5.f, GFloorTop + 8.f), true);
		}
	}

	// Mur nord, plein.
	Box(FVector(HL - T, -HW, 0), FVector(HL, HW, H), Palette::WoodLight);

	// Mur sud avec la porte.
	Box(FVector(-HL, -HW, 0), FVector(-HL + T, -GDoorHalfWidth, H), Palette::WoodLight);
	Box(FVector(-HL, GDoorHalfWidth, 0), FVector(-HL + T, HW, H), Palette::WoodLight);
	Box(FVector(-HL, -GDoorHalfWidth, GDoorHeight), FVector(-HL + T, GDoorHalfWidth, H), Palette::WoodLight);

	// Murs est et ouest, chacun percé d'une fenêtre centrée.
	const FLinearColor Shutter = FLinearColor::FromSRGBColor(FColor(86, 138, 146));
	for (const float Side : { -1.f, 1.f })
	{
		const float Y0 = Side > 0 ? HW - T : -HW;
		const float Y1 = Side > 0 ? HW : -HW + T;
		Box(FVector(-HL, Y0, 0), FVector(-GWindowHalfWidth, Y1, H), Palette::WoodLight);
		Box(FVector(GWindowHalfWidth, Y0, 0), FVector(HL, Y1, H), Palette::WoodLight);
		Box(FVector(-GWindowHalfWidth, Y0, 0), FVector(GWindowHalfWidth, Y1, GWindowSill), Palette::WoodLight);
		Box(FVector(-GWindowHalfWidth, Y0, GWindowTop), FVector(GWindowHalfWidth, Y1, H), Palette::WoodLight);

		// Encadrement débordant de chaque côté du mur, et croisillons.
		const float F0 = Y0 - 6.f;
		const float F1 = Y1 + 6.f;
		Box(FVector(-GWindowHalfWidth - 10, F0, GWindowSill - 10), FVector(GWindowHalfWidth + 10, F1, GWindowSill), Palette::WoodDark);
		Box(FVector(-GWindowHalfWidth - 10, F0, GWindowTop), FVector(GWindowHalfWidth + 10, F1, GWindowTop + 10), Palette::WoodDark);
		Box(FVector(-GWindowHalfWidth - 10, F0, GWindowSill), FVector(-GWindowHalfWidth, F1, GWindowTop), Palette::WoodDark);
		Box(FVector(GWindowHalfWidth, F0, GWindowSill), FVector(GWindowHalfWidth + 10, F1, GWindowTop), Palette::WoodDark);
		const float MidY = (Y0 + Y1) * 0.5f;
		Box(FVector(-3, MidY - 3, GWindowSill), FVector(3, MidY + 3, GWindowTop), Palette::WoodDark);
		Box(FVector(-GWindowHalfWidth, MidY - 3, (GWindowSill + GWindowTop) * 0.5f - 3), FVector(GWindowHalfWidth, MidY + 3, (GWindowSill + GWindowTop) * 0.5f + 3), Palette::WoodDark);

		// Volets ouverts à plat contre le mur extérieur : modèle dédié, sinon planches peintes.
		const float Outer = Side > 0 ? Y1 + 7.f : Y0 - 11.f;
		UStaticMesh* ShutterMesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_shutter"));
		for (const float SX : { -1.f, 1.f })
		{
			const float X0 = SX > 0 ? GWindowHalfWidth + 12 : -GWindowHalfWidth - 12 - GWindowHalfWidth;
			const FVector Center(X0 + GWindowHalfWidth * 0.5f, Outer + 2.f, GFloorTop + (GWindowSill + GWindowTop) * 0.5f);
			if (ShutterMesh)
			{
				// FitToLength met la largeur sur Y, façade vers −X : un quart de tour la tourne vers l'extérieur.
				const FVector Base(Center.X, Center.Y, GFloorTop + GWindowSill - 5.f);
				AddFitted(ShutterMesh, FitToLength(ShutterMesh, GWindowHalfWidth, GWindowTop - GWindowSill + 10.f), Base, Side > 0 ? -90.f : 90.f, false);
				continue;
			}
			UStaticMeshComponent* Panel = AddShape(this, Root, EShape::Cube, Center,
				FVector(GWindowHalfWidth, 4.f, GWindowTop - GWindowSill + 10.f), Shutter);
			Panel->SetMaterial(0, MakePaintedMaterial(Panel, TEXT("wood_planks"), 120.f, Shutter, Shutter * 1.6f));
		}

		// Jardinière fleurie sous la fenêtre : modèle dédié, sinon caisse et fleurs.
		const float BoxY = Side > 0 ? Y1 + 14.f : Y0 - 14.f;
		if (UStaticMesh* Planter = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_window_box")))
		{
			AddFitted(Planter, FitToLength(Planter, 2 * GWindowHalfWidth + 20.f, 50.f), FVector(0.f, BoxY + Side * 4.f, GFloorTop + GWindowSill - 40.f), Side > 0 ? -90.f : 90.f, false);
			continue;
		}
		Box(FVector(-GWindowHalfWidth, BoxY - 12, GWindowSill - 34), FVector(GWindowHalfWidth, BoxY + 12, GWindowSill - 12), Palette::WoodDark);
		int32 FlowerIndex = 0;
		for (const TCHAR* Name : { TEXT("SM_flower_white"), TEXT("SM_flower_blue"), TEXT("SM_flower_purple"), TEXT("SM_flower_white"), TEXT("SM_flower_blue") })
		{
			if (UStaticMesh* Flower = LoadGenerated<UStaticMesh>(TEXT("Meshes"), Name))
			{
				const float X = -GWindowHalfWidth + 25.f + FlowerIndex * 37.5f;
				AddGeneratedMesh(this, Root, Flower, FVector(X, BoxY, GFloorTop + GWindowSill - 14.f), 38.f, FlowerIndex * 67.f, false);
			}
			++FlowerIndex;
		}
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

	// Toit à deux pans, faîtage nord-sud.
	const float HalfSpan = HW + GRoofOverhang;
	const float SlopeRad = FMath::DegreesToRadians(GRoofSlope);
	const float SlabWidth = HalfSpan / FMath::Cos(SlopeRad);
	const float RidgeRise = HalfSpan * FMath::Tan(SlopeRad);
	for (const float Side : { -1.f, 1.f })
	{
		const FVector Center(0, Side * HalfSpan * 0.5f, GFloorTop + H + RidgeRise * 0.5f + 8.f);
		Paint(AddShape(this, Root, EShape::Cube, Center, FVector(2 * HL + 2 * GRoofOverhang, SlabWidth, 16), Palette::Roof,
			FRotator(0, 0, Side * GRoofSlope)), Palette::Roof);
	}

	// Hauteur du dessous du toit à l'aplomb des murs longs : la sablière comble ce jour.
	const float EaveGap = RidgeRise * GRoofOverhang / HalfSpan;
	for (const float Side : { -1.f, 1.f })
	{
		const float Y0 = Side > 0 ? HW - T : -HW;
		Box(FVector(-HL, Y0, H), FVector(HL, Y0 + T, H + EaveGap + 6.f), Palette::WoodDark);
	}

	// Pignons pleins au nord et au sud : un pentagone qui épouse le dessous du toit.
	UMaterialInterface* GableMaterial = MakePaintedMaterial(this, TEXT("wood_planks"), 220.f, Palette::WoodLight);
	const TArray<FVector2D> Gable = {
		FVector2D(-HW, GFloorTop + H),
		FVector2D(HW, GFloorTop + H),
		FVector2D(HW, GFloorTop + H + EaveGap),
		FVector2D(0.f, GFloorTop + H + RidgeRise - 2.f),
		FVector2D(-HW, GFloorTop + H + EaveGap),
	};
	AddPrism(this, Root, Gable, HL - T, HL, GableMaterial);
	AddPrism(this, Root, Gable, -HL, -HL + T, GableMaterial);

	// Plafond de planches (plancher du grenier) : de l'intérieur, on ne voit plus les tuiles.
	Box(FVector(-HL + T, -HW + T, H - 10.f), FVector(HL - T, HW - T, H), Palette::WoodLight);
	// Poutres apparentes sous le plafond.
	for (const float X : { -HL * 0.5f, 0.f, HL * 0.5f })
	{
		Box(FVector(X - 10.f, -HW + T, H - 32.f), FVector(X + 10.f, HW - T, H - 10.f), Palette::WoodDark);
	}

	// Comptoir entre l'entrée et le marchand.
	const FVector CounterCenter = ErewhonSector::MerchantLocation() - GetActorLocation() - FVector(120, 0, 0);
	if (UStaticMesh* Counter = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_counter")))
	{
		// Modèle Meshy, grand côté en travers de la pièce, façade vers la porte.
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetStaticMesh(Counter);
		Part->SetupAttachment(Root);
		Part->SetRelativeTransform(FitToLength(Counter, 300.f, 105.f) * FTransform(FRotator(0.f, CounterMeshYaw, 0.f), FVector(CounterCenter.X, 0.f, GFloorTop)));
		Part->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Part->RegisterComponent();
		AddInstanceComponent(Part);
	}
	else
	{
		Box(FVector(CounterCenter.X - 30, -150, 0), FVector(CounterCenter.X + 30, 150, 100), Palette::WoodDark);
		Box(FVector(CounterCenter.X - 40, -160, 100), FVector(CounterCenter.X + 40, 160, 108), Palette::WoodLight);
	}

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

	// Étagères construites en planches, garnies d'objets générés : bocaux, bouteilles, sacs de grain.
	// Repère d'une étagère : façade vers −X, largeur sur Y, pied à Z = 0.
	UStaticMesh* Jars = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_props_jars"));
	UStaticMesh* Bottles = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_props_bottles"));
	UStaticMesh* Sacks = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_props_sacks"));
	const auto Shelf = [&](const FVector& Location, float Yaw, int32 Variant)
	{
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

		// Garniture : sacs en bas, puis bocaux et bouteilles en alternance ; quelques trous.
		for (int32 Level = 0; Level < 4; ++Level)
		{
			const uint32 Pick = HashCombine(GetTypeHash(Variant), GetTypeHash(Level));
			if (Pick % 5 == 0 && Level > 0)
			{
				continue;   // étagère à demi vide, plus naturel
			}
			UStaticMesh* Prop = Level == 0 ? Sacks : (Pick % 2 == 0 ? Jars : Bottles);
			if (!Prop)
			{
				continue;
			}
			const float PropHeight = Level == 0 ? 40.f : 34.f;
			const float Top = Levels[Level] + Board * 0.5f;
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
			Part->SetStaticMesh(Prop);
			Part->SetupAttachment(Frame);
			Part->SetRelativeTransform(FitToLength(Prop, Width - 20.f, PropHeight) * FTransform(FVector(-2.f, 0.f, Top)));
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->RegisterComponent();
			AddInstanceComponent(Part);
		}
	};
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
	Shelf(FVector(Back, -150.f, GFloorTop), 0.f, 1);
	Shelf(FVector(Back, 150.f, GFloorTop), 0.f, 2);
	Shelf(FVector(-HL * 0.5f, HW - T - 20.f, GFloorTop), 90.f, 3);
	Shelf(FVector(-HL * 0.5f, -HW + T + 20.f, GFloorTop), -90.f, 4);
}

#undef LOCTEXT_NAMESPACE
