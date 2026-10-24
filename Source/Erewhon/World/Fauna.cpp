#include "World/Fauna.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonSettings.h"
#include "Sky/ErewhonWeather.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "World/ErewhonSound.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorLayout.h"
#include "World/Terrain.h"

using namespace ErewhonVisuals;

namespace ErewhonFauna
{
	const TArray<FSpecies>& Species()
	{
		// Par secteur : Spawn, Prairie, Road, Edge, Forest.
		static const TArray<FSpecies> List = {
			{ TEXT("fox"), 110.f, 420.f, 1400.f, { 0, 1, 0, 2, 1 }, TEXT("sniff"), TEXT("fox_yip") },
			{ TEXT("field_mouse"), 35.f, 170.f, 350.f, { 0, 5, 1, 3, 0 }, TEXT("sniff"), TEXT("mouse_squeak"), 0.1f },
			{ TEXT("wild_boar"), 95.f, 380.f, 1600.f, { 0, 0, 0, 1, 2 }, TEXT("sniff"), TEXT("boar_grunt") },
			{ TEXT("deer"), 110.f, 520.f, 2200.f, { 0, 0, 0, 1, 2 }, TEXT("graze"), TEXT("deer_bark") },
		};
		return List;
	}
}

namespace
{
	/** Le modèle (Blender, tête vers +Y) dans le repère d'Unreal : la tête vers +X de l'acteur. */
	constexpr float GBodyYaw = 90.f;
	constexpr int32 GRadius = 1;   // les animaux vivent dans les secteurs voisins du joueur
}

// ── L'animal ─────────────────────────────────────────────────────────────────

AFaunaAnimal::AFaunaAnimal()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

float AFaunaAnimal::GroundAt(const FVector2D& At) const
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	return State ? ErewhonTerrain::Height(At, State->GetWorldSeed()) : 0.f;
}

void AFaunaAnimal::BeginPlay()
{
	Super::BeginPlay();
	Rand.Initialize(GetUniqueID() * 31 + 7);
	const ErewhonFauna::FSpecies& Kind = ErewhonFauna::Species()[SpeciesIndex];
	const FString Character = Kind.Character;
	if (USkeletalMesh* Skeletal = LoadGenerated<USkeletalMesh>(*FString::Printf(TEXT("Characters/%s"), *Character), FString::Printf(TEXT("SK_%s"), *Character)))
	{
		Body = NewObject<USkeletalMeshComponent>(this, TEXT("Body"));
		Body->SetupAttachment(Root);
		Body->SetRelativeRotation(FRotator(0.f, GBodyYaw, 0.f));
		Body->SetSkeletalMesh(Skeletal);
		// Le squelette vient de Blender, en mètres : Unreal le lit en centimètres (cent fois trop petit).
		const float Fix = Skeletal->GetBounds().BoxExtent.Z < 10.f ? 100.f : 1.f;
		Body->SetRelativeScale3D(FVector(Fix * Kind.Scale));
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetCastShadow(true);
		Body->bVisibleInRayTracing = false;
		Body->RegisterComponent();
		for (const TCHAR* Gesture : { TEXT("idle"), TEXT("walk"), TEXT("trot"), TEXT("sniff"), TEXT("graze") })
		{
			if (UAnimSequence* Sequence = LoadGenerated<UAnimSequence>(*FString::Printf(TEXT("Characters/%s"), *Character), FString::Printf(TEXT("A_%s_%s"), *Character, Gesture)))
			{
				Animations.Add(Gesture, Sequence);
			}
		}
	}
	WaitLeft = Rand.FRandRange(1.f, 6.f);
	NextCall = Rand.FRandRange(20.f, 60.f);
	Target = FVector2D(GetActorLocation());
	Play(TEXT("idle"), true);
}

void AFaunaAnimal::Play(FName Name, bool bLoop, float Rate)
{
	const TObjectPtr<UAnimSequence>* Sequence = Animations.Find(Name);
	if (!Body || !Sequence || !*Sequence)
	{
		return;
	}
	if (Playing != Name)
	{
		Body->PlayAnimation(Sequence->Get(), bLoop);
		Playing = Name;
		PlayingUntil = bLoop ? 0.0 : GetWorld()->GetTimeSeconds() + (*Sequence)->GetPlayLength();
	}
	Body->SetPlayRate(Rate);
}

void AFaunaAnimal::PickTarget(bool bFlee, const FVector& Threat)
{
	const FVector2D Here(GetActorLocation());
	const float Size = UErewhonSettings::Get().SectorSize();
	const FVector2D Origin(ErewhonSector::Origin(HomeSector));
	FVector2D Next;
	if (bFlee)
	{
		// À l'opposé du joueur, un peu de travers, assez loin.
		const FVector2D Away = (Here - FVector2D(Threat)).GetSafeNormal().GetRotated(Rand.FRandRange(-35.f, 35.f));
		Next = Here + Away * Rand.FRandRange(1800.f, 3000.f);
	}
	else
	{
		// Une balade de quelques mètres, sans quitter son secteur.
		const float A = Rand.FRandRange(0.f, UE_TWO_PI);
		Next = Here + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rand.FRandRange(500.f, 1800.f);
	}
	Target = FVector2D(FMath::Clamp(Next.X, Origin.X + 200.0, Origin.X + Size - 200.0), FMath::Clamp(Next.Y, Origin.Y + 200.0, Origin.Y + Size - 200.0));
}

void AFaunaAnimal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ErewhonFauna::FSpecies& Kind = ErewhonFauna::Species()[SpeciesIndex];
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	const FVector2D Here(GetActorLocation());

	// Le joueur approche : il détale au trot.
	if (Player && !bTame && !bFleeing && FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) < Kind.FleeDistance)
	{
		bFleeing = true;
		WaitLeft = 0.f;
		PickTarget(true, Player->GetActorLocation());
	}

	const FVector2D ToTarget = Target - Here;
	const float Distance = ToTarget.Size();
	if (WaitLeft > 0.f || Distance < 30.f)
	{
		// À l'arrêt : attendre, flairer le sol, regarder autour.
		Speed = FMath::FInterpTo(Speed, 0.f, DeltaSeconds, 5.f);
		if (WaitLeft <= 0.f)
		{
			bFleeing = false;
			WaitLeft = bTame ? 0.4f : Rand.FRandRange(3.f, 10.f);
			if (!bTame && Rand.FRand() < 0.5f && Animations.Contains(Kind.Graze))
			{
				Playing = NAME_None;
				Play(Kind.Graze, false);
			}
		}
		WaitLeft -= DeltaSeconds;
		if (WaitLeft <= 0.f)
		{
			if (bTame)
			{
				// L'animal de test va et vient sur quatre mètres, pour qu'on voie sa marche.
				Target = FVector2D(GetActorLocation()) + FVector2D(0.f, Target.Y > GetActorLocation().Y - 1.f ? -400.f : 400.f);
				WaitLeft = 0.f;
			}
			else
			{
				PickTarget(false, FVector::ZeroVector);
			}
		}
		if (PlayingUntil <= 0.0 || GetWorld()->GetTimeSeconds() >= PlayingUntil)
		{
			Play(TEXT("idle"), true);
		}
	}
	else
	{
		// En route : marcher, ou trotter s'il fuit ; il se tourne doucement vers où il va.
		const float Wanted = bFleeing ? Kind.TrotSpeed : Kind.WalkSpeed;
		Speed = FMath::FInterpTo(Speed, Wanted, DeltaSeconds, 3.f);
		const FVector2D Step = ToTarget / Distance * FMath::Min(Distance, Speed * DeltaSeconds);
		FRotator Facing = GetActorRotation();
		Facing.Yaw = FMath::FixedTurn(Facing.Yaw, FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X)), (bFleeing ? 360.f : 140.f) * DeltaSeconds);
		const FVector2D NewXY = Here + Step;
		SetActorLocationAndRotation(FVector(NewXY, GroundAt(NewXY)), Facing);
		if (bFleeing || Speed > Kind.WalkSpeed * 1.5f)
		{
			Play(TEXT("trot"), true, FMath::Clamp(Speed / Kind.TrotSpeed, 0.6f, 1.4f));
		}
		else
		{
			Play(TEXT("walk"), true, FMath::Clamp(Speed / Kind.WalkSpeed, 0.5f, 1.4f));
		}
	}

	// De temps en temps, son cri.
	NextCall -= DeltaSeconds;
	if (NextCall <= 0.f)
	{
		NextCall = Rand.FRandRange(40.f, 120.f);
		ErewhonSound::PlayAt(this, ErewhonSound::Load(Kind.Call), GetActorLocation(), 0.7f, 300.f, 3500.f);
	}
}

// ── La buse ──────────────────────────────────────────────────────────────────

namespace
{
	constexpr float GBuzzardSpan = 125.f;     // envergure (cm)
	constexpr float GBuzzardMeshYaw = 180.f;  // comme les petits oiseaux : le bec vers +X
	constexpr float GSoarSpeed = 900.f;       // cm/s
	constexpr float GDiveSpeed = 2200.f;
	constexpr float GClimbSpeed = 650.f;
	/** Sous l'orage : durée de son départ (ou de son retour) (s), distance où elle file (cm), seuil où on la cache. */
	constexpr float GBuzzardShelterSeconds = 25.f;
	constexpr float GBuzzardShelterDistance = 30000.f;
	constexpr float GBuzzardShelterHidden = 0.98f;
}

ABuzzard::ABuzzard()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

float ABuzzard::GroundAt(const FVector2D& At) const
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	return State ? ErewhonTerrain::Height(At, State->GetWorldSeed()) : 0.f;
}

void ABuzzard::BeginPlay()
{
	Super::BeginPlay();
	Rand.Initialize(GetUniqueID() + 1789);
	if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_buzzard")))
	{
		Model = NewObject<UStaticMeshComponent>(this);
		Model->SetStaticMesh(Mesh);
		Model->SetupAttachment(Root);
		Model->SetRelativeTransform(FitToLength(Mesh, GBuzzardSpan) * FTransform(FRotator(0.f, GBuzzardMeshYaw, 0.f)));
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Model->SetCastShadow(true);
		Model->RegisterComponent();
		const FVector Size = Mesh->GetBoundingBox().GetSize();
		Flap = Model->CreateAndSetMaterialInstanceDynamic(0);
		if (Flap)
		{
			Flap->SetScalarParameterValue(TEXT("FlapAmount"), FlapAmount);
			Flap->SetScalarParameterValue(TEXT("SpanIsX"), Size.X > Size.Y ? 1.f : 0.f);
			Flap->SetScalarParameterValue(TEXT("Phase"), Rand.FRand() * 6.f);
		}
	}
	Center = FVector2D(GetActorLocation());
	Radius = Rand.FRandRange(2500.f, 4000.f);
	Altitude = Rand.FRandRange(3500.f, 5500.f);
	NextDive = Rand.FRandRange(30.f, 70.f);
	const float AwayAngle = Rand.FRandRange(0.f, UE_TWO_PI);
	Away = FVector2D(FMath::Cos(AwayAngle), FMath::Sin(AwayAngle));
	if (bTest)
	{
		Radius = 700.f;
		Altitude = 500.f;
		NextDive = 1.0e6f;
	}
}

void ABuzzard::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (Player)
	{
		// Le centre de ses cercles suit le joueur, sans se presser.
		Center = bTest ? FVector2D(Player->GetActorLocation()) + FVector2D(1500.f, 0.f)
			: FMath::Vector2DInterpTo(Center, FVector2D(Player->GetActorLocation()) + FVector2D(1500.f, 800.f), DeltaSeconds, 0.05f);
	}
	// Sous l'orage ou une forte averse, elle file au loin en descendant, puis disparaît ; elle revient quand
	// le temps se calme (la météo est la même sur chaque machine).
	if (const AErewhonGameState* State = AErewhonGameState::Get(this))
	{
		const float Sheltering = ErewhonWeather::ShelterTarget(State->SampleWeather());
		Shelter = Shelter < 0.f ? Sheltering : FMath::FInterpConstantTo(Shelter, Sheltering, DeltaSeconds, 1.f / GBuzzardShelterSeconds);
	}
	const float Sheltered = FMath::Max(Shelter, 0.f);
	const FVector2D Refuge = Away * (GBuzzardShelterDistance * Sheltered * Sheltered);
	SetActorHiddenInGame(Sheltered >= GBuzzardShelterHidden);
	FVector Location = GetActorLocation();
	FVector Velocity = FVector::ZeroVector;
	float Bank = 0.f;
	// En fuite, elle bat des ailes contre le vent.
	float WantedFlap = FMath::Max(0.06f, 0.6f * Sheltered);
	switch (Mode)
	{
	case EMode::Soar:
	{
		// De grands cercles, penchée dans le virage ; quelques battements de temps en temps.
		Angle += GSoarSpeed / Radius * DeltaSeconds;
		const FVector2D OnCircle = Center + Refuge + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
		const FVector Wanted(OnCircle, GroundAt(OnCircle) + FMath::Lerp(Altitude + 400.f * FMath::Sin(Angle * 0.7f), 1500.f, Sheltered));
		const FVector Next = FMath::VInterpTo(Location, Wanted, DeltaSeconds, 2.f);
		Velocity = (Next - Location) / FMath::Max(DeltaSeconds, 0.001f);
		Location = Next;
		Bank = -22.f;
		if ((NextFlaps -= DeltaSeconds) <= 0.f)
		{
			NextFlaps = Rand.FRandRange(6.f, 14.f);
			FlapBurst = Rand.FRandRange(1.f, 2.2f);
		}
		if (Sheltered <= 0.f && (NextDive -= DeltaSeconds) <= 0.f)
		{
			// Une proie dans l'herbe, un peu devant : elle fond dessus.
			const FVector2D Ahead = FVector2D(Location) + FVector2D(FMath::Cos(Angle + UE_HALF_PI), FMath::Sin(Angle + UE_HALF_PI)) * Rand.FRandRange(1500.f, 3000.f);
			Prey = FVector(Ahead, GroundAt(Ahead) + 40.f);
			Mode = EMode::Dive;
			ErewhonSound::PlayAt(this, ErewhonSound::Load(TEXT("buzzard_cry")), Location, 1.f, 3000.f, 15000.f);
		}
		break;
	}
	case EMode::Dive:
	{
		// Ailes repliées, droit sur la proie.
		const FVector ToPrey = Prey - Location;
		const float Step = GDiveSpeed * DeltaSeconds;
		WantedFlap = 0.f;
		if (ToPrey.Size() <= Step)
		{
			Location = Prey;
			Mode = EMode::Climb;
			Pause = 0.6f;
		}
		else
		{
			Velocity = ToPrey.GetSafeNormal() * GDiveSpeed;
			Location += Velocity * DeltaSeconds;
		}
		break;
	}
	case EMode::Climb:
	{
		// Un instant au sol, puis elle remonte en battant fort des ailes.
		if ((Pause -= DeltaSeconds) > 0.f)
		{
			WantedFlap = 0.f;
			break;
		}
		const FVector2D OnCircle = Center + Refuge + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
		const FVector Wanted(OnCircle, GroundAt(OnCircle) + Altitude);
		const FVector ToWanted = Wanted - Location;
		Velocity = ToWanted.GetSafeNormal() * GClimbSpeed;
		Location += Velocity * DeltaSeconds;
		WantedFlap = 1.f;
		if (ToWanted.Size() < 400.f)
		{
			Mode = EMode::Soar;
			NextDive = Rand.FRandRange(40.f, 90.f);
		}
		break;
	}
	}
	if (FlapBurst > 0.f)
	{
		FlapBurst -= DeltaSeconds;
		WantedFlap = FMath::Max(WantedFlap, 0.8f);
	}
	FlapAmount = FMath::FInterpTo(FlapAmount, WantedFlap, DeltaSeconds, 3.f);
	if (Flap)
	{
		Flap->SetScalarParameterValue(TEXT("FlapAmount"), FlapAmount);
	}
	FRotator Facing = GetActorRotation();
	if (!Velocity.IsNearlyZero(10.f))
	{
		Facing = Velocity.Rotation();
		Facing.Pitch = FMath::Clamp(Facing.Pitch, -60.f, 40.f);
		Facing.Roll = Bank;
	}
	SetActorLocationAndRotation(Location, FMath::RInterpTo(GetActorRotation(), Facing, DeltaSeconds, 3.f));
	// Son cri, au-dessus des prés.
	if ((NextCry -= DeltaSeconds) <= 0.f && Sheltered < 0.3f)
	{
		NextCry = Rand.FRandRange(15.f, 40.f);
		ErewhonSound::PlayAt(this, ErewhonSound::Load(TEXT("buzzard_cry")), Location, 0.9f, 3000.f, 15000.f);
	}
}

// ── Le peuplement ────────────────────────────────────────────────────────────

AFaunaManager::AFaunaManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AFaunaManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if ((RefreshTimer -= DeltaSeconds) > 0.f)
	{
		return;
	}
	RefreshTimer = 2.f;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!State || !Player)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	// -ErewhonTestFauna : tests, un animal apprivoisé de chaque espèce sur la route du nord, près de la place.
	if (!bTestSpawned && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestFauna")))
	{
		bTestSpawned = true;
		for (int32 Index = 0; Index < ErewhonFauna::Species().Num(); ++Index)
		{
			const FVector2D At = FVector2D(ErewhonSector::Center(FIntPoint::ZeroValue)) + FVector2D(1350.f + 380.f * Index, Index % 2 ? 120.f : -120.f);
			const FTransform Spawn(FRotator(0.f, 90.f, 0.f), FVector(At, ErewhonTerrain::Height(At, State->GetWorldSeed())));
			AFaunaAnimal* Animal = GetWorld()->SpawnActorDeferred<AFaunaAnimal>(AFaunaAnimal::StaticClass(), Spawn);
			Animal->SpeciesIndex = Index;
			Animal->HomeSector = ErewhonSector::FromWorld(FVector(At, 0.f));
			Animal->bTame = true;
			Animal->FinishSpawning(Spawn);
		}
	}
#endif
	// Une buse au-dessus du joueur, toujours.
	if (!Buzzard.IsValid())
	{
		const FVector At = Player->GetActorLocation() + FVector(2000.f, 1000.f, 4500.f);
		ABuzzard* Bird = GetWorld()->SpawnActorDeferred<ABuzzard>(ABuzzard::StaticClass(), FTransform(At));
#if !UE_BUILD_SHIPPING
		// -ErewhonTestBuzzard : tests, la buse tourne bas, à quinze mètres devant le joueur.
		Bird->bTest = FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestBuzzard"));
#endif
		Bird->FinishSpawning(FTransform(At));
		Buzzard = Bird;
	}
	const FIntPoint Here = ErewhonSector::FromWorld(Player->GetActorLocation());
	// Les secteurs trop loin : leurs animaux s'en vont.
	for (auto It = Populated.CreateIterator(); It; ++It)
	{
		if (FMath::Abs(It.Key().X - Here.X) > GRadius + 1 || FMath::Abs(It.Key().Y - Here.Y) > GRadius + 1)
		{
			for (const TWeakObjectPtr<AFaunaAnimal>& Animal : It.Value())
			{
				if (Animal.IsValid())
				{
					Animal->Destroy();
				}
			}
			It.RemoveCurrent();
		}
	}
	// Les secteurs proches : leurs animaux, toujours les mêmes pour un secteur donné (même tirage).
	const float Size = UErewhonSettings::Get().SectorSize();
	for (int32 DY = -GRadius; DY <= GRadius; ++DY)
	{
		for (int32 DX = -GRadius; DX <= GRadius; ++DX)
		{
			const FIntPoint Coord = Here + FIntPoint(DX, DY);
			if (Populated.Contains(Coord))
			{
				continue;
			}
			TArray<TWeakObjectPtr<AFaunaAnimal>>& Animals = Populated.Add(Coord);
			const ESectorType Type = ErewhonSector::TypeAt(Coord, State->GetWorldSeed());
			FRandomStream Rand(HashCombine(GetTypeHash(Coord), GetTypeHash(State->GetWorldSeed())) ^ 0xFA0A);
			const TArray<ErewhonFauna::FSpecies>& All = ErewhonFauna::Species();
			for (int32 Index = 0; Index < All.Num(); ++Index)
			{
				const int32 Count = All[Index].PerSector[FMath::Clamp(static_cast<int32>(Type), 0, 4)];
				for (int32 i = 0; i < Count; ++i)
				{
					// Pas tous les secteurs n'en ont : une chance sur deux pour chacun.
					if (Rand.FRand() < 0.5f)
					{
						continue;
					}
					const FVector2D At = FVector2D(ErewhonSector::Origin(Coord)) + FVector2D(Rand.FRandRange(0.15f, 0.85f), Rand.FRandRange(0.15f, 0.85f)) * Size;
					if (FVector::Dist2D(Player->GetActorLocation(), FVector(At, 0.f)) < 2500.f)
					{
						continue;   // pas sous le nez du joueur
					}
					const FTransform Spawn(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), FVector(At, ErewhonTerrain::Height(At, State->GetWorldSeed())));
					AFaunaAnimal* Animal = GetWorld()->SpawnActorDeferred<AFaunaAnimal>(AFaunaAnimal::StaticClass(), Spawn);
					Animal->SpeciesIndex = Index;
					Animal->HomeSector = Coord;
					Animal->FinishSpawning(Spawn);
					Animals.Add(Animal);
				}
			}
		}
	}
}
