#include "World/Villagers.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "World/Door.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorLayout.h"
#include "World/SectorTypes.h"
#include "World/SpawnLayout.h"
#include "World/Terrain.h"
#include "World/VillageNetwork.h"

#define LOCTEXT_NAMESPACE "ErewhonVillagers"

using namespace ErewhonVisuals;

namespace ErewhonVillagers
{
	namespace
	{
		/** Un point du plan du village (depuis le centre de la place), en monde. */
		FVector2D VillagersPlanPoint(const FIntPoint& Village, const FVector2D& Local)
		{
			return FVector2D(ErewhonVillages::VillageFrame(Village).TransformPosition(FVector(Local, 0.0)));
		}

		/** Une direction du plan du village, en monde. */
		FVector2D VillagersPlanDir(const FIntPoint& Village, const FVector2D& Local)
		{
			return FVector2D(ErewhonVillages::VillageFrame(Village).TransformVectorNoScale(FVector(Local, 0.0)));
		}

		/** Chaque maison : son centre (depuis la place), son orientation (la porte, −X, vers la route), sa route. */
		struct FHouseSpot { FVector2D Offset; float Yaw; FVector2D Road; };
		const FHouseSpot GHouses[HouseCount] = {
			{ FVector2D(7500.f, -1300.f), -90.f, FVector2D(1.f, 0.f) },    // route du nord, côté ouest
			{ FVector2D(1300.f, 7500.f), 0.f, FVector2D(0.f, 1.f) },       // route de l'est, côté nord
			{ FVector2D(-1300.f, -7500.f), 180.f, FVector2D(0.f, -1.f) },  // route de l'ouest, côté sud
		};

		const FResident GResidents[HouseCount] = {
			{ TEXT("villager_baker_tp"), TEXT("Marthe"), TEXT("la boulangère") },
			{ TEXT("villager_gardener_tp"), TEXT("Anselme"), TEXT("le jardinier") },
			{ TEXT("villager_weaver_tp"), TEXT("Lison"), TEXT("la tisserande") },
		};

		/** Le marché, depuis la place (repère du village). */
		const FVector2D GMarketOffset(3300.f, 3300.f);
	}

	const FResident& Resident(int32 Index)
	{
		return GResidents[FMath::Clamp(Index, 0, HouseCount - 1)];
	}

	FTransform HouseTransform(const FIntPoint& Village, int32 Index)
	{
		const FHouseSpot& Spot = GHouses[FMath::Clamp(Index, 0, HouseCount - 1)];
		// Le sol est aplani tout autour (voir ErewhonTerrain) : la maison est posée à Z = 0.
		const FTransform House = FTransform(FRotator(0.f, Spot.Yaw, 0.f), FVector(Spot.Offset, 0.f)) * ErewhonVillages::VillageFrame(Village);
		return FTransform(House.GetRotation(), FVector(House.GetLocation().X, House.GetLocation().Y, 0.f));
	}

	FVector2D Doorstep(const FIntPoint& Village, int32 Index)
	{
		return FVector2D(HouseTransform(Village, Index).TransformPosition(FVector(-ErewhonSpawn::CabinHalfLength - 220.f, 0.f, 0.f)));
	}

	FVector2D RoadDir(const FIntPoint& Village, int32 Index)
	{
		return VillagersPlanDir(Village, GHouses[FMath::Clamp(Index, 0, HouseCount - 1)].Road);
	}

	FVector2D LaneEnd(const FIntPoint& Village, int32 Index)
	{
		// Le pied du sentier, sur l'axe de la route.
		const FVector2D Plaza(ErewhonSector::Center(ErewhonVillages::CenterOf(Village)));
		const FVector2D Road = RoadDir(Village, Index);
		const FVector2D Step = Doorstep(Village, Index) - Plaza;
		return Plaza + Road * FVector2D::DotProduct(Step, Road);
	}

	FVector2D MarketCenter(const FIntPoint& Village)
	{
		return VillagersPlanPoint(Village, GMarketOffset);
	}

	FVector2D MarketJunction(const FIntPoint& Village)
	{
		// Sur la route du nord (l'axe X du repère du village), à la hauteur du marché.
		return VillagersPlanPoint(Village, FVector2D(GMarketOffset.X, 0.f));
	}

	FVector2D MarketEntry(const FIntPoint& Village)
	{
		return VillagersPlanPoint(Village, GMarketOffset - FVector2D(0.f, MarketRadius * 0.6f));
	}

	FTransform StallTransform(const FIntPoint& Village, int32 Index)
	{
		// En couronne, le comptoir (−X) tourné vers le centre ; on laisse le côté de l'arrivée (vers la route du nord) libre.
		const float Angle = 30.f + 360.f / StallCount * Index;
		const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(Angle)), FMath::Sin(FMath::DegreesToRadians(Angle)));
		const FTransform Stall = FTransform(FRotator(0.f, Angle, 0.f), FVector(GMarketOffset + Dir * 880.f, 0.f)) * ErewhonVillages::VillageFrame(Village);
		return FTransform(Stall.GetRotation(), FVector(Stall.GetLocation().X, Stall.GetLocation().Y, 0.f));
	}

	void AddGrounds(const FIntPoint& Coord, TArray<FClearing>& Clearings, TArray<FBox2D>& Paths, TArray<bool>& Paved)
	{
		// Les maisons et le marché sont à moins de deux secteurs de la place : loin des villages, rien à faire.
		const FIntPoint Village = ErewhonVillages::NearestVillage(Coord);
		const FIntPoint FromCenter = Coord - ErewhonVillages::CenterOf(Village);
		if (FMath::Max(FMath::Abs(FromCenter.X), FMath::Abs(FromCenter.Y)) > 1)
		{
			return;
		}
		for (int32 i = 0; i < HouseCount; ++i)
		{
			const FTransform House = HouseTransform(Village, i);
			if (ErewhonSector::FromWorld(House.GetLocation()) != Coord)
			{
				continue;
			}
			// De la terre autour de la maison, et un sentier du perron à la route.
			Clearings.Add({ FVector2D(House.GetLocation()), 780.f, false });
			const FVector2D From = Doorstep(Village, i);
			const FVector2D To = LaneEnd(Village, i);
			Paths.Add(FBox2D(FVector2D(FMath::Min(From.X, To.X), FMath::Min(From.Y, To.Y)) - FVector2D(140.f), FVector2D(FMath::Max(From.X, To.X), FMath::Max(From.Y, To.Y)) + FVector2D(140.f)));
			Paved.Add(false);
		}
		if (ErewhonSector::FromWorld(FVector(MarketCenter(Village), 0.f)) == Coord)
		{
			// La place du marché, et son chemin jusqu'à la route du nord (dans le repère du village : le long de Y).
			Clearings.Add({ MarketCenter(Village), MarketRadius, false });
			Paths.Add(ErewhonSpawn::VillageBox(Village, FBox2D(FVector2D(GMarketOffset.X - 200.f, 0.f), FVector2D(GMarketOffset.X + 200.f, GMarketOffset.Y - MarketRadius + 200.f))));
			Paved.Add(false);
		}
	}
}

// ── L'habitant ───────────────────────────────────────────────────────────────

namespace
{
	constexpr float GWalkSpeed = 115.f;   // cm/s, une promenade
	constexpr float GHalfHeight = 88.f;
	/** Où l'on se tient chez soi (repère de la maison) : près du lit, de la table, de la fenêtre, de l'étagère. */
	const FVector2D GInside[] = { FVector2D(220.f, -170.f), FVector2D(30.f, 120.f), FVector2D(-150.f, -200.f), FVector2D(150.f, 190.f), FVector2D(-60.f, 0.f) };
	/** Ce qu'il dit quand on lui parle. */
	const TCHAR* const GGreetings[] = {
		TEXT("Bonjour ! Belle journée pour se promener, n'est-ce pas ?"),
		TEXT("Ah, vous voilà ! Le marché sera bientôt plein d'étals, on dit."),
		TEXT("Le vent tourne… il fera doux ce soir."),
		TEXT("Vous avez vu les champs ? Ça pousse bien, cette saison."),
		TEXT("Prenez soin de vous, et des bhuraks !"),
	};
}

AVillager::AVillager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(34.f, GHalfHeight);
	Capsule->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RootComponent = Capsule;
}

void AVillager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVillager, ResidentIndex);
	DOREPLIFETIME(AVillager, Village);
}

void AVillager::BeginPlay()
{
	Super::BeginPlay();
	Rand.Initialize(1000 + ResidentIndex * 77 + FMath::Rand() % 1000);
	LastLocation = GetActorLocation();
	if (GetNetMode() != NM_DedicatedServer)
	{
		const FString Character = ErewhonVillagers::Resident(ResidentIndex).Character;
		if (USkeletalMesh* Skeletal = LoadGenerated<USkeletalMesh>(*FString::Printf(TEXT("Characters/%s"), *Character), FString::Printf(TEXT("SK_%s"), *Character)))
		{
			Body = NewObject<USkeletalMeshComponent>(this, TEXT("Body"));
			Body->SetupAttachment(Capsule);
			Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -GHalfHeight), FRotator(0.f, -90.f, 0.f));
			Body->SetSkeletalMesh(Skeletal);
			Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Body->RegisterComponent();
			AddInstanceComponent(Body);
			for (const TCHAR* Gesture : { TEXT("idle"), TEXT("walk"), TEXT("look") })
			{
				if (UAnimSequence* Sequence = LoadGenerated<UAnimSequence>(*FString::Printf(TEXT("Characters/%s"), *Character), FString::Printf(TEXT("A_%s_%s"), *Character, Gesture)))
				{
					Animations.Add(Gesture, Sequence);
				}
			}
			Play(TEXT("idle"), true);
		}
	}
	if (HasAuthority())
	{
		// Chez soi, d'abord, quelques minutes.
		HomeLeft = Rand.FRandRange(20.f, 90.f);
#if !UE_BUILD_SHIPPING
		// -ErewhonTestVillagers : tests, ils sortent tout de suite.
		if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestVillagers")))
		{
			HomeLeft = 0.f;
		}
#endif
		PlanAtHome();
	}
}

float AVillager::GroundZ(const FVector2D& At) const
{
	// Chez soi : le plancher ; sur le perron, entre les deux ; dehors, le terrain.
	const FTransform House = ErewhonVillagers::HouseTransform(Village, ResidentIndex);
	const FVector Local = House.InverseTransformPosition(FVector(At, 0.f));
	const float Floor = House.GetLocation().Z + ErewhonSpawn::CabinFloorTop;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const float Ground = State ? ErewhonTerrain::Height(At, State->GetWorldSeed()) : 0.f;
	if (FMath::Abs(Local.Y) < ErewhonSpawn::CabinHalfWidth + 20.f && Local.X > -ErewhonSpawn::CabinHalfLength && Local.X < ErewhonSpawn::CabinHalfLength)
	{
		return Floor;
	}
	if (FMath::Abs(Local.Y) < 110.f && Local.X > -ErewhonSpawn::CabinHalfLength - 210.f && Local.X <= -ErewhonSpawn::CabinHalfLength)
	{
		const float T = (Local.X + ErewhonSpawn::CabinHalfLength + 210.f) / 210.f;
		return FMath::Lerp(Ground, Floor, T);
	}
	return Ground;
}

void AVillager::PlanAtHome()
{
	// D'un coin de la maison à un autre.
	const FTransform House = ErewhonVillagers::HouseTransform(Village, ResidentIndex);
	Route.Reset();
	Route.Add(FVector2D(House.TransformPosition(FVector(GInside[Rand.RandHelper(UE_ARRAY_COUNT(GInside))], 0.f))));
	NextPoint = 0;
	Place = EPlace::Home;
}

void AVillager::PlanOuting()
{
	using namespace ErewhonVillagers;
	const FTransform House = HouseTransform(Village, ResidentIndex);
	const FVector2D Plaza(ErewhonSector::Center(ErewhonVillages::CenterOf(Village)));
	// La route du nord du village (celle du marché), en monde : le village peut être tourné.
	const FVector2D North = VillagersPlanDir(Village, FVector2D(1.f, 0.f));
	Route.Reset();
	if (Place != EPlace::Out)
	{
		// Sortir : la porte, le perron, le sentier, la route jusqu'à la place.
		Route.Add(FVector2D(House.TransformPosition(FVector(-ErewhonSpawn::CabinHalfLength + 70.f, 0.f, 0.f))));
		Route.Add(Doorstep(Village, ResidentIndex));
		Route.Add(LaneEnd(Village, ResidentIndex));
		Route.Add(Plaza + RoadDir(Village, ResidentIndex) * 1250.f);
	}
	else
	{
		// D'un arrêt à l'autre : on revient d'abord vers la place par où l'on est venu.
		const FVector2D Here(GetActorLocation());
		if (FVector2D::Distance(Here, MarketCenter(Village)) < MarketRadius + 300.f)
		{
			Route.Add(MarketEntry(Village));
			Route.Add(MarketJunction(Village));
			Route.Add(Plaza + North * 1250.f);
		}
		else if (FVector2D::Distance(Here, Plaza) > 1400.f)
		{
			const FVector2D Away = Here - Plaza;
			Route.Add(Plaza + (FMath::Abs(Away.X) > FMath::Abs(Away.Y) ? FVector2D(FMath::Sign(Away.X), 0.f) : FVector2D(0.f, FMath::Sign(Away.Y))) * 1250.f);
		}
	}
	const FVector2D From = Route.Num() > 0 ? Route.Last() : FVector2D(GetActorLocation());
	// Faire le tour de la place jusqu'à une sortie (en couronne, sans couper au plus court).
	const auto AroundTo = [&](const FVector2D& Exit)
	{
		const float A0 = FMath::Atan2(From.Y - Plaza.Y, From.X - Plaza.X);
		const float A1 = FMath::Atan2(Exit.Y - Plaza.Y, Exit.X - Plaza.X);
		const float Delta = FMath::FindDeltaAngleRadians(A0, A1);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(FMath::Abs(Delta) / FMath::DegreesToRadians(40.f)));
		for (int32 s = 1; s < Steps; ++s)
		{
			const float A = A0 + Delta * s / Steps;
			Route.Add(Plaza + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 760.f);
		}
		Route.Add(Exit);
	};
	// Où aller : la place, le marché, ou plus loin sur une route du village.
	const float Pick = Rand.FRand();
	if (Pick < 0.35f)
	{
		const float A = Rand.FRandRange(0.f, UE_TWO_PI);
		AroundTo(Plaza + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rand.FRandRange(700.f, 1050.f));
	}
	else if (Pick < 0.7f)
	{
		AroundTo(Plaza + North * 1250.f);
		Route.Add(MarketJunction(Village));
		Route.Add(MarketEntry(Village));
		const float A = Rand.FRandRange(0.f, UE_TWO_PI);
		Route.Add(MarketCenter(Village) + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rand.FRandRange(100.f, 600.f));
	}
	else
	{
		const FVector2D Roads[] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
		const FVector2D Road = Roads[Rand.RandHelper(4)];
		AroundTo(Plaza + Road * 1250.f);
		Route.Add(Plaza + Road * Rand.FRandRange(4000.f, 14000.f) + FVector2D(-Road.Y, Road.X) * Rand.FRandRange(-120.f, 120.f));
	}
	NextPoint = 0;
	Place = EPlace::Out;
}

void AVillager::PlanReturn()
{
	using namespace ErewhonVillagers;
	const FTransform House = HouseTransform(Village, ResidentIndex);
	const FVector2D Plaza(ErewhonSector::Center(ErewhonVillages::CenterOf(Village)));
	const FVector2D Here(GetActorLocation());
	Route.Reset();
	// Revenir vers la place par le chemin pris (le marché : par son chemin ; une route : par elle).
	if (FVector2D::Distance(Here, MarketCenter(Village)) < MarketRadius + 300.f)
	{
		Route.Add(MarketEntry(Village));
		Route.Add(MarketJunction(Village));
		Route.Add(Plaza + VillagersPlanDir(Village, FVector2D(1.f, 0.f)) * 1250.f);
	}
	else if (FVector2D::Distance(Here, Plaza) > 1400.f)
	{
		const FVector2D Away = (Here - Plaza);
		const FVector2D Road = FMath::Abs(Away.X) > FMath::Abs(Away.Y) ? FVector2D(FMath::Sign(Away.X), 0.f) : FVector2D(0.f, FMath::Sign(Away.Y));
		Route.Add(Plaza + Road * 1250.f);
	}
	// Le tour de la place jusqu'à sa route, puis chez soi.
	const FVector2D From = Route.Num() > 0 ? Route.Last() : Here;
	const FVector2D Exit = Plaza + RoadDir(Village, ResidentIndex) * 1250.f;
	const float A0 = FMath::Atan2(From.Y - Plaza.Y, From.X - Plaza.X);
	const float A1 = FMath::Atan2(Exit.Y - Plaza.Y, Exit.X - Plaza.X);
	const float Delta = FMath::FindDeltaAngleRadians(A0, A1);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt32(FMath::Abs(Delta) / FMath::DegreesToRadians(40.f)));
	for (int32 s = 1; s < Steps; ++s)
	{
		const float A = A0 + Delta * s / Steps;
		Route.Add(Plaza + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 760.f);
	}
	Route.Add(Exit);
	Route.Add(LaneEnd(Village, ResidentIndex));
	Route.Add(Doorstep(Village, ResidentIndex));
	Route.Add(FVector2D(House.TransformPosition(FVector(-ErewhonSpawn::CabinHalfLength + 70.f, 0.f, 0.f))));
	Route.Add(FVector2D(House.TransformPosition(FVector(GInside[Rand.RandHelper(UE_ARRAY_COUNT(GInside))], 0.f))));
	NextPoint = 0;
	Place = EPlace::Returning;
}

void AVillager::HandleDoor()
{
	// Il ouvre sa porte en arrivant devant, la referme une fois passé.
	ADoor* Door = HouseDoor.Get();
	if (!Door)
	{
		return;
	}
	const float Distance = FVector::Dist2D(Door->GetActorLocation(), GetActorLocation());
	if (Distance < 220.f && !Door->IsOpen())
	{
		Door->Toggle();
		bOpenedDoor = true;
	}
	else if (bOpenedDoor && Distance > 380.f)
	{
		if (Door->IsOpen())
		{
			Door->Toggle();
		}
		bOpenedDoor = false;
	}
}

void AVillager::StepServer(float DeltaSeconds)
{
	HandleDoor();
	if (WaitLeft > 0.f)
	{
		WaitLeft -= DeltaSeconds;
		return;
	}
	if (!Route.IsValidIndex(NextPoint))
	{
		// Arrivé : on s'attarde, puis la suite.
		switch (Place)
		{
		case EPlace::Home:
			HomeLeft -= 1.f;   // (compté en pauses)
			WaitLeft = Rand.FRandRange(4.f, 10.f);
			HomeLeft -= WaitLeft;
			if (HomeLeft <= 0.f)
			{
				OutingStops = Rand.RandRange(1, 3);
				PlanOuting();
			}
			else
			{
				PlanAtHome();
			}
			break;
		case EPlace::Out:
			WaitLeft = Rand.FRandRange(8.f, 22.f);
			if (--OutingStops > 0)
			{
				PlanOuting();
			}
			else
			{
				PlanReturn();
			}
			break;
		case EPlace::Returning:
			WaitLeft = Rand.FRandRange(4.f, 8.f);
			HomeLeft = Rand.FRandRange(60.f, 180.f);
			PlanAtHome();
			break;
		}
		return;
	}
	const FVector2D Here(GetActorLocation());
	const FVector2D Target = Route[NextPoint];
	const FVector2D ToTarget = Target - Here;
	const float Distance = ToTarget.Size();
	const float Step = GWalkSpeed * DeltaSeconds;
	FVector2D NewXY = Here;
	if (Distance <= Step)
	{
		NewXY = Target;
		++NextPoint;
	}
	else
	{
		NewXY = Here + ToTarget / Distance * Step;
	}
	// Il se tourne doucement vers où il va.
	FRotator Facing = GetActorRotation();
	if (Distance > 1.f)
	{
		const float Wanted = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));
		Facing.Yaw = FMath::FixedTurn(Facing.Yaw, Wanted, 240.f * DeltaSeconds);
	}
	SetActorLocationAndRotation(FVector(NewXY, GroundZ(NewXY) + GHalfHeight), Facing);
}

void AVillager::Play(FName Name, bool bLoop, float Rate)
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

void AVillager::StepAnimation(float DeltaSeconds)
{
	// La vitesse se lit au déplacement (le mouvement vient du serveur).
	const FVector Location = GetActorLocation();
	const float Speed = DeltaSeconds > 0.f ? FVector::Dist2D(Location, LastLocation) / DeltaSeconds : 0.f;
	LastLocation = Location;
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Speed, DeltaSeconds, 6.f);
	const double Now = GetWorld()->GetTimeSeconds();
	if (SmoothedSpeed > 30.f)
	{
		Play(TEXT("walk"), true, FMath::Clamp(SmoothedSpeed / 120.f, 0.6f, 1.4f));
		NextLook = Now + FMath::FRandRange(5.f, 12.f);
		return;
	}
	if (PlayingUntil > 0.0 && Now < PlayingUntil)
	{
		return;
	}
	if (Now >= NextLook && Animations.Contains(TEXT("look")))
	{
		NextLook = Now + FMath::FRandRange(8.f, 16.f);
		Playing = NAME_None;
		Play(TEXT("look"), false);
		return;
	}
	Play(TEXT("idle"), true);
}

void AVillager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		StepServer(DeltaSeconds);
	}
	if (Body)
	{
		StepAnimation(DeltaSeconds);
	}
}

FText AVillager::GetInteractionLabel() const
{
	const ErewhonVillagers::FResident& Who = ErewhonVillagers::Resident(ResidentIndex);
	return FText::Format(LOCTEXT("TalkTo", "Parler à {0}, {1}"), FText::FromString(Who.Name), FText::FromString(Who.Trade));
}

void AVillager::Interact(AErewhonPlayerController* Controller)
{
	if (Controller)
	{
		const ErewhonVillagers::FResident& Who = ErewhonVillagers::Resident(ResidentIndex);
		Controller->ShowToast(FText::Format(LOCTEXT("Says", "{0} : « {1} »"), FText::FromString(Who.Name),
			FText::FromString(GGreetings[FMath::RandHelper(UE_ARRAY_COUNT(GGreetings))])));
	}
}

#undef LOCTEXT_NAMESPACE
