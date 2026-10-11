#include "World/Wildlife.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Sky/CelestialSystem.h"
#include "World/ErewhonVisuals.h"
#include "World/Terrain.h"
#include "Components/AudioComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sky/ErewhonWind.h"
#include "Sound/SoundWave.h"
#include "World/ErewhonSound.h"
#include "World/SectorLayout.h"
#include "World/SpawnLayout.h"

using namespace ErewhonVisuals;

namespace
{
	constexpr int32 GButterflyCount = 24;
	constexpr int32 GFlockCount = 3;
	constexpr int32 GBirdsPerFlock = 6;
	constexpr int32 GSoloBirds = 6;

	/** Envergure d'un oiseau (cm). */
	constexpr float GBirdSpan = 70.f;

	/** Le modèle généré regarde vers −Y une fois mis à la longueur : un quart de tour l'aligne sur +X. */
	constexpr float GBirdMeshYaw = 180.f;   // FitToLength met l'envergure sur Y : le bec doit pointer vers +X

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

	BirdMesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bird"));
	if (BirdMesh)
	{
		// Envergure sur Y ; le matériau a besoin de savoir sur quel axe local se trouvent les ailes.
		const FVector Size = BirdMesh->GetBoundingBox().GetSize();
		bBirdSpanAlongX = Size.X > Size.Y;
		BirdFit = FitToLength(BirdMesh, GBirdSpan) * FTransform(FRotator(0.f, GBirdMeshYaw, 0.f));
	}

	for (int32 Flock = 0; Flock < GFlockCount; ++Flock)
	{
		for (int32 i = 0; i < GBirdsPerFlock; ++i)
		{
			FCreature Bird = MakeBird();
			// Formation en V : rang i en arrière, de part et d'autre de la tête du vol.
			const float Row = static_cast<float>((i + 1) / 2);
			const float Side = i == 0 ? 0.f : (i % 2 == 0 ? 1.f : -1.f);
			Bird.Home = FVector(-Row * 140.f, Side * Row * 110.f, Rand.FRandRange(-50.f, 50.f));
			Bird.Flock = Flock;
			Birds.Add(Bird);
		}
	}
	// Des solitaires, chacun sur son propre cercle, plus bas et plus libres.
	for (int32 i = 0; i < GSoloBirds; ++i)
	{
		FCreature Bird = MakeBird();
		Bird.Home = FVector(Rand.FRandRange(-6000.f, 6000.f), Rand.FRandRange(-6000.f, 6000.f), 0.f);
		Bird.Radius = Rand.FRandRange(900.f, 2800.f);
		Bird.Altitude = Rand.FRandRange(700.f, 2200.f);
		Bird.Speed = Rand.FRandRange(0.12f, 0.3f) * (Rand.FRand() < 0.5f ? -1.f : 1.f);
#if !UE_BUILD_SHIPPING
		if (i == 0 && FParse::Param(FCommandLine::Get(), TEXT("ErewhonBirdTest")))
		{
			// Captures : un oiseau qui tourne tout près, devant la caméra.
			Bird.Home = FVector(600.f, 0.f, 0.f);
			Bird.Radius = 250.f;
			Bird.Altitude = 250.f;
			Bird.Speed = 0.35f;
		}
#endif
		Birds.Add(Bird);
	}

	// Ambiance : vent et herbe en boucle, non spatialisés.
	const auto Loop = [this](const TCHAR* Name)
	{
		UAudioComponent* Audio = NewObject<UAudioComponent>(this);
		Audio->SetupAttachment(RootComponent);
		Audio->SetSound(ErewhonSound::Load(Name));
		Audio->bAllowSpatialization = false;
		Audio->bIsUISound = false;
		Audio->SetVolumeMultiplier(0.f);
		Audio->RegisterComponent();
		Audio->Play(Rand.FRandRange(0.f, 5.f));
		return Audio;
	};
	WindAudio = Loop(TEXT("amb_wind"));
	GrassAudio = Loop(TEXT("amb_grass"));
}

AWildlife::FCreature AWildlife::MakeBird()
{
	if (!BirdMesh)
	{
		// Repli : silhouette procédurale.
		FCreature Bird = MakeCreature(FLinearColor::FromSRGBColor(FColor(70, 76, 92)), FLinearColor::FromSRGBColor(FColor(36, 38, 48)), 55.f, true);
		Bird.Phase = Rand.FRandRange(0.f, UE_TWO_PI);
		return Bird;
	}
	FCreature Bird;
	USceneComponent* Body = NewObject<USceneComponent>(this);
	Body->SetupAttachment(RootComponent);
	Body->RegisterComponent();
	AddInstanceComponent(Body);
	Bird.Body = Body;

	UStaticMeshComponent* Model = NewObject<UStaticMeshComponent>(this);
	Model->SetStaticMesh(BirdMesh);
	Model->SetupAttachment(Body);
	Model->SetRelativeTransform(BirdFit);
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Model->SetCastShadow(false);
	// Paramètres lus par M_BirdFlap : amplitude du battement, axe des ailes, phase.
	Bird.Phase = Rand.FRandRange(0.f, UE_TWO_PI);
	UMaterialInstanceDynamic* Flap = Model->CreateAndSetMaterialInstanceDynamic(0);
	if (Flap)
	{
		Flap->SetScalarParameterValue(TEXT("FlapAmount"), 1.f);
		Flap->SetScalarParameterValue(TEXT("SpanIsX"), bBirdSpanAlongX ? 1.f : 0.f);
		Flap->SetScalarParameterValue(TEXT("Phase"), Bird.Phase);
	}
	Bird.Flap = Flap;
	Model->RegisterComponent();
	AddInstanceComponent(Model);
	Bird.Model = Model;
	return Bird;
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
	const ACelestialSystem* Celestial = ACelestialSystem::Find(this);
	const bool bDay = !Celestial || Celestial->GetSunOrbit().Direction(Clock).Z > 0.05f;
	if (bDay != bVisible)
	{
		bVisible = bDay;
		for (const FCreature& Creature : Butterflies)
		{
			Creature.Body->SetVisibility(bDay, true);
		}
		for (const FCreature& Creature : Birds)
		{
			Creature.Body->SetVisibility(bDay, true);
		}
	}
	UpdateAmbience(Pawn->GetActorLocation(), Clock, DeltaSeconds, bDay);
	if (!bDay)
	{
		return;
	}

	const double Time = GetWorld()->GetTimeSeconds();
	UpdateButterflies(Pawn->GetActorLocation(), Time);
	UpdateBirds(Pawn->GetActorLocation(), Time, DeltaSeconds);
}

void AWildlife::UpdateAmbience(const FVector& Focus, double Clock, float DeltaSeconds, bool bDay)
{
	const float Ambient = ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Ambient);

	// Le vent souffle plus ou moins fort avec le vent du monde.
	const float Strength = FMath::Clamp(static_cast<float>(ErewhonWind::Velocity(Clock).Size() / (ErewhonWind::BaseSpeed + ErewhonWind::SpeedAmplitude)), 0.f, 1.f);
	if (WindAudio)
	{
		WindAudio->SetVolumeMultiplier(FMath::Lerp(0.25f, 0.75f, Strength) * Ambient);
	}

	// L'herbe bruisse quand on avance dans la prairie (pas sur les chemins).
	float Target = 0.f;
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (Character && State && Character->GetCharacterMovement()->IsMovingOnGround())
	{
		const float Speed = Character->GetVelocity().Size2D();
		const TSharedRef<const FSectorLayout> Layout = GetWorld()->GetSubsystem<USectorLayoutSubsystem>()->GetLayout(ErewhonSector::FromWorld(Focus), State->GetWorldSeed());
		const bool bInGrass = Layout->DirtAmount(FVector2D(Focus)) < 0.4f && !ErewhonSpawn::IsInsideCabin(Focus);
		Target = bInGrass ? FMath::Clamp(Speed / 400.f, 0.f, 1.f) : 0.f;
	}
	GrassLevel = FMath::FInterpTo(GrassLevel, Target, DeltaSeconds, 4.f);
	if (GrassAudio)
	{
		GrassAudio->SetVolumeMultiplier(GrassLevel * 0.9f * ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Effects));
	}

	// De temps en temps, un oiseau chante près de là.
	NextChirp -= DeltaSeconds;
	if (bDay && NextChirp <= 0.f && Birds.Num() > 0)
	{
		NextChirp = Rand.FRandRange(3.f, 9.f);
		const FCreature& Singer = Birds[Rand.RandHelper(Birds.Num())];
		const bool bSong = Rand.FRand() < 0.3f;
		USoundWave* Sound = bSong ? ErewhonSound::LoadVariant(TEXT("bird_song"), 2) : ErewhonSound::LoadVariant(TEXT("bird_chirp"), 3);
		// Souvent au-dessus du joueur plutôt qu'au loin, pour qu'on l'entende vraiment.
		const FVector Where = Rand.FRand() < 0.6f
			? Focus + FVector(Rand.FRandRange(-1500.f, 1500.f), Rand.FRandRange(-1500.f, 1500.f), Rand.FRandRange(500.f, 1200.f))
			: Singer.Body->GetComponentLocation();
		ErewhonSound::PlayAt(this, Sound, Where, bSong ? 0.7f : 0.55f, 800.f, 6000.f);
	}
}

void AWildlife::UpdateButterflies(const FVector& Focus, double Time)
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

void AWildlife::UpdateBirds(const FVector& Focus, double Time, float DeltaSeconds)
{
	if (BirdAnchor.IsZero() || FVector::Dist2D(BirdAnchor, Focus) > GBirdAnchorDrift)
	{
		BirdAnchor = Focus;
	}
	const float Ground = GroundAt(FVector2D(BirdAnchor));
	for (FCreature& Bird : Birds)
	{
		FVector Location;
		FRotator Facing;
		float Bank;
		if (Bird.Flock != INDEX_NONE)
		{
			// Chaque vol tourne en grand cercle, à sa propre altitude et à son rythme.
			const int32 Flock = Bird.Flock;
			const float Radius = 2500.f + Flock * 1100.f;
			const float Angular = (Flock % 2 == 0 ? 1.f : -1.f) * (0.09f + Flock * 0.015f);
			const float Angle = static_cast<float>(Time) * Angular + Flock * 2.1f;
			const FVector Center = BirdAnchor + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			Facing = FRotator(0.f, FMath::RadiansToDegrees(Angle + (Angular > 0.f ? UE_HALF_PI : -UE_HALF_PI)), 0.f);
			const float Altitude = 1800.f + Flock * 400.f + 120.f * FMath::Sin(static_cast<float>(Time) * 0.3f + Flock);
			Location = Center + Facing.RotateVector(Bird.Home) + FVector(0.f, 0.f, Altitude + Ground);
			Bank = Angular > 0.f ? -14.f : 14.f;
		}
		else
		{
			// Solitaire : un cercle qui lui est propre, qui ondule en altitude et dérive un peu.
			const float Angle = static_cast<float>(Time) * Bird.Speed + Bird.Phase;
			const FVector Center = BirdAnchor + Bird.Home + FVector(FMath::Sin(static_cast<float>(Time) * 0.05f + Bird.Phase) * 800.f, 0.f, 0.f);
			const float Radius = Bird.Radius * (1.f + 0.25f * FMath::Sin(static_cast<float>(Time) * 0.2f + Bird.Phase));
			Location = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius,
				Bird.Altitude + Ground + 250.f * FMath::Sin(static_cast<float>(Time) * 0.4f + Bird.Phase));
			Facing = FRotator(0.f, FMath::RadiansToDegrees(Angle + (Bird.Speed > 0.f ? UE_HALF_PI : -UE_HALF_PI)), 0.f);
			Bank = Bird.Speed > 0.f ? -22.f : 22.f;
		}
		Bird.Body->SetWorldLocationAndRotation(Location, Facing + FRotator(0.f, 0.f, Bank));

		// Battements par séries, entrecoupés de vol plané.
		const float T = static_cast<float>(Time) + Bird.Phase;
		const bool bGliding = FMath::Sin(T * 0.7f) < -0.3f;
		if (Bird.Model)
		{
			Bird.FlapAmount = FMath::FInterpTo(Bird.FlapAmount, bGliding ? 0.08f : 1.f, DeltaSeconds, 3.f);
			if (Bird.Flap)
			{
				Bird.Flap->SetScalarParameterValue(TEXT("FlapAmount"), Bird.FlapAmount);
			}
		}
		else
		{
			const float Flap = bGliding ? 6.f : 40.f * FMath::Sin(T * 9.f);
			Bird.WingLeft->SetRelativeRotation(FRotator(0.f, 0.f, Flap));
			Bird.WingRight->SetRelativeRotation(FRotator(0.f, 0.f, -Flap));
		}
	}
}
