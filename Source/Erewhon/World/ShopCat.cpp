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

	// Respiration : le dos se soulève à peine.
	const float Breath = 1.f + 0.02f * FMath::Sin(GetWorld()->GetTimeSeconds() * 2.4f);
	Body->SetRelativeScale3D(FVector(BaseScale.X, BaseScale.Y * Breath, BaseScale.Z * Breath));
}

FText AShopCat::GetInteractionLabel() const
{
	return LOCTEXT("Pet", "Caresser la chatte");
}

void AShopCat::Interact(AErewhonPlayerController* Controller)
{
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
