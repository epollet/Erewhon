#include "World/ShopCat.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Engine/StaticMesh.h"
#include "World/ErewhonVisuals.h"

#define LOCTEXT_NAMESPACE "ErewhonShopCat"

using namespace ErewhonVisuals;

namespace
{
	/** Elle reste à un endroit environ deux minutes et demie. */
	constexpr double GSpotSeconds = 150.0;
	constexpr float GCatHeight = 36.f;

	/** Le modèle généré regarde vers +Y : un quart de tour l'aligne sur l'axe de l'acteur. */
	constexpr float GMeshYaw = 90.f;
}

AShopCat::AShopCat()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(18.f, 18.f);
	Capsule->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RootComponent = Capsule;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Capsule);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AShopCat::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cat_tabby")))
	{
		Body->SetStaticMesh(Mesh);
		// Assise, pattes au ras du bas de la capsule.
		Body->SetRelativeTransform(FitToHeight(Mesh, GCatHeight) * FTransform(FRotator(0.f, GMeshYaw, 0.f), FVector(0.f, 0.f, -Capsule->GetScaledCapsuleHalfHeight())));
		BaseScale = Body->GetRelativeScale3D();
		BaseTransform = Body->GetRelativeTransform();
	}
}

void AShopCat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (Spots.IsEmpty() || !State)
	{
		return;
	}

	// Même tirage pour tous les joueurs : l'horloge du monde décide de sa place.
	const int64 Period = static_cast<int64>(State->GetWorldClock() / GSpotSeconds);
	const int32 Spot = static_cast<int32>(GetTypeHash(Period) % static_cast<uint32>(Spots.Num()));
	if (Spot != CurrentSpot)
	{
		CurrentSpot = Spot;
		const FTransform& Where = Spots[Spot];
		SetActorLocationAndRotation(Where.GetLocation() + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight()), Where.GetRotation());
	}

	// L'attente du moment, tirée au hasard quand la précédente se termine.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= IdleEnd)
	{
		const float Roll = FMath::FRand();
		Idle = Roll < 0.35f ? EIdle::Sit : Roll < 0.5f ? EIdle::LookLeft : Roll < 0.65f ? EIdle::LookRight
			: Roll < 0.78f ? EIdle::Stretch : Roll < 0.92f ? EIdle::Loaf : EIdle::Twitch;
		IdleStart = Now;
		IdleEnd = Now + (Idle == EIdle::Loaf ? FMath::FRandRange(10.f, 20.f) : Idle == EIdle::Twitch ? 1.2f : FMath::FRandRange(3.f, 7.f));
	}
	const float T = static_cast<float>(Now - IdleStart);
	FVector4 Wanted(0.f, 0.f, 0.f, 1.f);   // lacet, tangage, roulis, hauteur
	switch (Idle)
	{
	case EIdle::LookLeft:  Wanted.X = -28.f; break;
	case EIdle::LookRight: Wanted.X = 28.f; break;
	case EIdle::Stretch:   Wanted.Y = -9.f; Wanted.W = 0.88f; break;   // le nez vers l'avant, le dos qui s'allonge
	case EIdle::Loaf:      Wanted.W = 0.72f; break;                    // en boule
	case EIdle::Twitch:    Wanted.Z = FMath::Sin(T * 40.f) * 4.f * (1.f - T / 1.2f); break;
	default: break;
	}
	const float Blend = FMath::Clamp(DeltaSeconds * 3.f, 0.f, 1.f);
	Pose = Pose + (Wanted - Pose) * Blend;
	if (Idle == EIdle::Twitch)
	{
		Pose.Z = Wanted.Z;
	}
	// Respiration : le dos se soulève à peine.
	const float Breath = 1.f + 0.02f * FMath::Sin(Now * 2.4f);
	FTransform Posed = BaseTransform * FTransform(FRotator(Pose.Y, Pose.X, Pose.Z));
	const FVector Scale = BaseScale * FVector(Idle == EIdle::Stretch ? 1.f + (1.f - Pose.W) : 1.f, Breath, Breath * Pose.W);
	Posed.SetScale3D(Scale);
	Body->SetRelativeTransform(Posed);
}

FText AShopCat::GetInteractionLabel() const
{
	return LOCTEXT("Pet", "Caresser la chatte");
}

void AShopCat::Interact(AErewhonPlayerController* Controller)
{
	// Caressée, elle frissonne d'aise.
	Idle = EIdle::Twitch;
	IdleStart = GetWorld()->GetTimeSeconds();
	IdleEnd = IdleStart + 1.2;
	if (Controller)
	{
		static const FText Reactions[] = {
			LOCTEXT("Purr", "La chatte ronronne doucement… rrrrr."),
			LOCTEXT("Blink", "Elle cligne lentement de ses yeux jaunes."),
			LOCTEXT("Head", "Elle vient frotter sa tête contre votre main."),
		};
		Controller->ShowToast(Reactions[FMath::RandHelper(UE_ARRAY_COUNT(Reactions))]);
	}
}

#undef LOCTEXT_NAMESPACE
