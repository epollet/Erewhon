#include "World/Wildlife.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Sky/CelestialSystem.h"
#include "World/ErewhonVisuals.h"
#include "World/Terrain.h"

using namespace ErewhonVisuals;

namespace
{
	constexpr int32 GButterflyCount = 24;
	constexpr int32 GFlockCount = 4;
	constexpr int32 GBirdsPerFlock = 6;

	/** Rayons d'apparition des papillons autour du joueur (cm). */
	constexpr float GButterflyMin = 300.f;
	constexpr float GButterflyMax = 1600.f;
	constexpr float GButterflyForget = 3500.f;

	/** Les vols d'oiseaux se recentrent quand le joueur s'en éloigne trop (cm). */
	constexpr float GBirdAnchorDrift = 15000.f;

	/** Couleurs douces de papillons : intérieur de l'aile, puis bord. */
	const TPair<FLinearColor, FLinearColor> GButterflyColors[] = {
		{ FLinearColor::FromSRGBColor(FColor(250, 246, 230)), FLinearColor::FromSRGBColor(FColor(236, 226, 196)) },   // piéride blanche
		{ FLinearColor::FromSRGBColor(FColor(250, 222, 90)), FLinearColor::FromSRGBColor(FColor(236, 170, 40)) },    // citron
		{ FLinearColor::FromSRGBColor(FColor(140, 190, 250)), FLinearColor::FromSRGBColor(FColor(70, 110, 210)) },    // azuré
		{ FLinearColor::FromSRGBColor(FColor(246, 150, 70)), FLinearColor::FromSRGBColor(FColor(70, 50, 40)) },       // aurore
	};
}

AWildlife::AWildlife()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AWildlife::BeginPlay()
{
	Super::BeginPlay();
	Rand.Initialize(0x51AD);
	WingMaterial = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_VertexColor"));

	for (int32 i = 0; i < GButterflyCount; ++i)
	{
		const auto& Colors = GButterflyColors[i % UE_ARRAY_COUNT(GButterflyColors)];
		FCreature Butterfly = MakeCreature(Colors.Key, Colors.Value, Rand.FRandRange(9.f, 13.f), false);
		Butterfly.Phase = Rand.FRandRange(0.f, UE_TWO_PI);
		Butterfly.Speed = Rand.FRandRange(0.7f, 1.3f);
		Butterfly.Radius = Rand.FRandRange(150.f, 320.f);
		Butterflies.Add(Butterfly);
	}

	const FLinearColor Back = FLinearColor::FromSRGBColor(FColor(70, 76, 92));
	const FLinearColor Tip = FLinearColor::FromSRGBColor(FColor(36, 38, 48));
	for (int32 Flock = 0; Flock < GFlockCount; ++Flock)
	{
		for (int32 i = 0; i < GBirdsPerFlock; ++i)
		{
			FCreature Bird = MakeCreature(Back, Tip, 55.f, true);
			// Formation en V : rang i en arrière, de part et d'autre de la tête du vol.
			const float Row = static_cast<float>((i + 1) / 2);
			const float Side = i == 0 ? 0.f : (i % 2 == 0 ? 1.f : -1.f);
			Bird.Home = FVector(-Row * 160.f, Side * Row * 130.f, Rand.FRandRange(-60.f, 60.f));
			Bird.Phase = Rand.FRandRange(0.f, UE_TWO_PI);
			Bird.Speed = Flock;   // index du vol
			Birds.Add(Bird);
		}
	}
}

AWildlife::FCreature AWildlife::MakeCreature(const FLinearColor& Inner, const FLinearColor& Outer, float Span, bool bBird)
{
	FCreature Creature;
	USceneComponent* Body = NewObject<USceneComponent>(this);
	Body->SetupAttachment(RootComponent);
	Body->RegisterComponent();
	AddInstanceComponent(Body);
	Creature.Body = Body;

	// Corps : un fuseau sombre dans l'axe du vol (+X).
	const FVector BodySize = bBird ? FVector(Span * 0.55f, Span * 0.16f, Span * 0.14f) : FVector(Span * 0.5f, Span * 0.1f, Span * 0.1f);
	UStaticMeshComponent* Torso = AddShape(this, Body, EShape::Sphere, FVector::ZeroVector, BodySize, bBird ? Outer : FLinearColor(0.08f, 0.07f, 0.06f), FRotator::ZeroRotator, false);
	Torso->SetCastShadow(false);

	Creature.WingLeft = MakeWing(Body, Inner, Outer, Span, -1.f, bBird);
	Creature.WingRight = MakeWing(Body, Inner, Outer, Span, 1.f, bBird);
	return Creature;
}

UProceduralMeshComponent* AWildlife::MakeWing(USceneComponent* Parent, const FLinearColor& Inner, const FLinearColor& Outer, float Span, float Side, bool bBird)
{
	// Aile dans le plan XY, charnière sur l'axe du corps, déployée vers ±Y.
	TArray<FVector> Points;
	if (bBird)
	{
		// Aile effilée d'hirondelle : bord d'attaque droit, bord de fuite en flèche.
		Points = { FVector(6, 0, 0), FVector(Span * 0.12f, Span * Side, 0), FVector(-Span * 0.3f, Span * 0.85f * Side, 0), FVector(-8, 0, 0) };
	}
	else
	{
		// Aile antérieure arrondie et aile postérieure plus courte.
		Points = { FVector(0, 0, 0), FVector(Span * 0.55f, Span * 0.85f * Side, 0), FVector(Span * 0.1f, Span * Side, 0),
			FVector(-Span * 0.15f, Span * 0.75f * Side, 0), FVector(-Span * 0.5f, Span * 0.45f * Side, 0) };
	}
	TArray<FLinearColor> Colors;
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		Colors.Add(i == 0 || i == Points.Num() - 1 ? Inner : Outer);
	}
	TArray<int32> Triangles;
	for (int32 i = 1; i + 1 < Points.Num(); ++i)
	{
		// Le matériau est double face : l'ordre des sommets importe peu.
		Triangles.Append({ 0, i, i + 1 });
	}
	TArray<FVector> Normals;
	Normals.Init(FVector::UpVector, Points.Num());

	UProceduralMeshComponent* Wing = NewObject<UProceduralMeshComponent>(this);
	Wing->SetupAttachment(Parent);
	Wing->CreateMeshSection_LinearColor(0, Points, Triangles, Normals, {}, Colors, {}, false);
	Wing->SetMaterial(0, WingMaterial ? WingMaterial.Get() : static_cast<UMaterialInterface*>(MakeColorMaterial(Wing, Outer)));
	Wing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Wing->SetCastShadow(false);
	Wing->RegisterComponent();
	AddInstanceComponent(Wing);
	return Wing;
}

float AWildlife::GroundAt(const FVector2D& World) const
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	return State ? ErewhonTerrain::Height(World, State->GetWorldSeed()) : 0.f;
}

void AWildlife::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!Pawn || !State)
	{
		return;
	}

	// La nuit, tout le monde se repose.
	const double Clock = State->GetWorldClock();
	const ACelestialSystem* Sky = ACelestialSystem::Find(this);
	const bool bDay = !Sky || Sky->GetSunOrbit().Direction(Clock).Z > 0.05f;
	if (bDay != bVisible)
	{
		bVisible = bDay;
		SetActorHiddenInGame(!bDay);
	}
	if (!bDay)
	{
		return;
	}

	const double Time = GetWorld()->GetTimeSeconds();
	UpdateButterflies(Pawn->GetActorLocation(), Time, DeltaSeconds);
	UpdateBirds(Pawn->GetActorLocation(), Time);
}

void AWildlife::UpdateButterflies(const FVector& Focus, double Time, float DeltaSeconds)
{
	for (FCreature& Butterfly : Butterflies)
	{
		// Trop loin (ou pas encore placé) : il réapparaît ailleurs autour du joueur.
		if (FVector::Dist2D(Butterfly.Home, Focus) > GButterflyForget || Butterfly.Home.IsZero())
		{
			const float Angle = Rand.FRandRange(0.f, UE_TWO_PI);
			const float Distance = Rand.FRandRange(GButterflyMin, GButterflyMax);
			Butterfly.Home = Focus + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Distance;
		}

		// Une ronde paresseuse en huit, ponctuée de petits sursauts.
		const float T = static_cast<float>(Time) * Butterfly.Speed + Butterfly.Phase;
		const FVector2D Offset(FMath::Sin(T * 0.6f) * Butterfly.Radius, FMath::Sin(T * 1.2f) * Butterfly.Radius * 0.6f);
		const FVector2D Position = FVector2D(Butterfly.Home) + Offset;
		// Au-dessus des herbes hautes (jusqu'à 1,5 m), là où on les voit danser.
		const float Height = 165.f + 35.f * FMath::Sin(T * 1.7f) + 12.f * FMath::Sin(T * 5.3f);
		const FVector Location(Position, GroundAt(Position) + Height);

		const FVector2D Velocity(FMath::Cos(T * 0.6f) * 0.6f * Butterfly.Radius, FMath::Cos(T * 1.2f) * 1.2f * 0.6f * Butterfly.Radius);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
		Butterfly.Body->SetWorldLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));

		const float Flap = 15.f + 55.f * FMath::Abs(FMath::Sin(T * 13.f));
		Butterfly.WingLeft->SetRelativeRotation(FRotator(0.f, 0.f, Flap));
		Butterfly.WingRight->SetRelativeRotation(FRotator(0.f, 0.f, -Flap));
	}
}

void AWildlife::UpdateBirds(const FVector& Focus, double Time)
{
	if (BirdAnchor.IsZero() || FVector::Dist2D(BirdAnchor, Focus) > GBirdAnchorDrift)
	{
		BirdAnchor = Focus;
	}
	for (FCreature& Bird : Birds)
	{
		// Chaque vol tourne en grand cercle, à sa propre altitude et à son rythme.
		const int32 Flock = static_cast<int32>(Bird.Speed);
		const float Radius = 2500.f + Flock * 1000.f;
		const float Angular = (Flock % 2 == 0 ? 1.f : -1.f) * (0.09f + Flock * 0.015f);
		const float Angle = static_cast<float>(Time) * Angular + Flock * 2.1f;
		const FVector Center = BirdAnchor + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		const float Heading = Angle + (Angular > 0.f ? UE_HALF_PI : -UE_HALF_PI);
		const FRotator Facing(0.f, FMath::RadiansToDegrees(Heading), 0.f);
		const float Altitude = 1800.f + Flock * 400.f + 120.f * FMath::Sin(static_cast<float>(Time) * 0.3f + Flock);
		const FVector Location = Center + Facing.RotateVector(Bird.Home) + FVector(0.f, 0.f, Altitude + GroundAt(FVector2D(BirdAnchor)));
		Bird.Body->SetWorldLocationAndRotation(Location, Facing + FRotator(0.f, 0.f, Angular > 0.f ? -12.f : 12.f));

		// Battements par séries, entrecoupés de vol plané.
		const float T = static_cast<float>(Time) + Bird.Phase;
		const bool bGliding = FMath::Sin(T * 0.7f) < -0.3f;
		const float Flap = bGliding ? 6.f : 40.f * FMath::Sin(T * 9.f);
		Bird.WingLeft->SetRelativeRotation(FRotator(0.f, 0.f, Flap));
		Bird.WingRight->SetRelativeRotation(FRotator(0.f, 0.f, -Flap));
	}
}
