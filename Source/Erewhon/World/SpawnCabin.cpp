#include "World/SpawnCabin.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/ErewhonPlayerController.h"
#include "Engine/World.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorTypes.h"
#include "World/SpawnLayout.h"

#define LOCTEXT_NAMESPACE "ErewhonCabin"

using namespace ErewhonVisuals;

namespace
{
	constexpr float GFloorTop = 30.f;     // plancher surélevé
	constexpr float GWallThickness = 20.f;
	constexpr float GDoorHalfWidth = 70.f;
	constexpr float GDoorHeight = 220.f;
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
	Lamp->SetRelativeLocation(FVector(0, 0, GFloorTop + 260.f));
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

void ASpawnCabin::BuildCabin()
{
	using namespace ErewhonSpawn;
	const float HL = CabinHalfLength;
	const float HW = CabinHalfWidth;
	const float H = CabinWallHeight;
	const float T = GWallThickness;

	// Une boîte de mur entre deux bornes, en coordonnées locales (Z relatif au plancher).
	const auto Box = [this](const FVector& Min, const FVector& Max, const FLinearColor& Color)
	{
		const FVector Center = (Min + Max) * 0.5f + FVector(0, 0, GFloorTop);
		AddShape(this, Root, EShape::Cube, Center, Max - Min, Color);
	};

	// Socle de pierre et marche devant la porte.
	AddShape(this, Root, EShape::Cube, FVector(0, 0, GFloorTop * 0.5f), FVector(2 * HL + 40, 2 * HW + 40, GFloorTop), Palette::Stone);
	AddShape(this, Root, EShape::Cube, FVector(-HL - 60, 0, GFloorTop * 0.25f), FVector(80, 2 * GDoorHalfWidth + 40, GFloorTop * 0.5f), Palette::Stone);

	// Mur nord, plein.
	Box(FVector(HL - T, -HW, 0), FVector(HL, HW, H), Palette::WoodLight);

	// Mur sud avec la porte.
	Box(FVector(-HL, -HW, 0), FVector(-HL + T, -GDoorHalfWidth, H), Palette::WoodLight);
	Box(FVector(-HL, GDoorHalfWidth, 0), FVector(-HL + T, HW, H), Palette::WoodLight);
	Box(FVector(-HL, -GDoorHalfWidth, GDoorHeight), FVector(-HL + T, GDoorHalfWidth, H), Palette::WoodLight);

	// Murs est et ouest, chacun percé d'une fenêtre centrée.
	for (const float Side : { -1.f, 1.f })
	{
		const float Y0 = Side > 0 ? HW - T : -HW;
		const float Y1 = Side > 0 ? HW : -HW + T;
		Box(FVector(-HL, Y0, 0), FVector(-GWindowHalfWidth, Y1, H), Palette::WoodLight);
		Box(FVector(GWindowHalfWidth, Y0, 0), FVector(HL, Y1, H), Palette::WoodLight);
		Box(FVector(-GWindowHalfWidth, Y0, 0), FVector(GWindowHalfWidth, Y1, GWindowSill), Palette::WoodLight);
		Box(FVector(-GWindowHalfWidth, Y0, GWindowTop), FVector(GWindowHalfWidth, Y1, H), Palette::WoodLight);
	}

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
		AddShape(this, Root, EShape::Cube, Center, FVector(2 * HL + 2 * GRoofOverhang, SlabWidth, 16), Palette::Roof,
			FRotator(0, 0, Side * GRoofSlope));
	}

	// Pignons en gradins sous le toit, au nord et au sud : chaque gradin reste sous la pente.
	constexpr int32 GableSteps = 4;
	for (const float SX : { -1.f, 1.f })
	{
		for (int32 Step = 0; Step < GableSteps; ++Step)
		{
			const float Z0 = H + RidgeRise * Step / GableSteps;
			const float Z1 = H + RidgeRise * (Step + 1) / GableSteps;
			const float HalfWidthAtStep = HW * (1.f - (Step + 1.f) / GableSteps);
			if (HalfWidthAtStep <= 0.f)
			{
				continue;
			}
			const float X0 = SX > 0 ? HL - T : -HL;
			Box(FVector(X0, -HalfWidthAtStep, Z0), FVector(X0 + T, HalfWidthAtStep, Z1), Palette::WoodDark);
		}
	}

	// Comptoir entre l'entrée et le marchand.
	const FVector CounterCenter = ErewhonSector::MerchantLocation() - GetActorLocation() - FVector(120, 0, 0);
	Box(FVector(CounterCenter.X - 30, -150, 0), FVector(CounterCenter.X + 30, 150, 100), Palette::WoodDark);
	Box(FVector(CounterCenter.X - 40, -160, 100), FVector(CounterCenter.X + 40, 160, 108), Palette::WoodLight);
}

#undef LOCTEXT_NAMESPACE
