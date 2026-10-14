#include "World/Bhurak.h"

#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundWave.h"
#include "World/ErewhonSound.h"
#include "World/ErewhonVisuals.h"
#include "World/Storage.h"

#define LOCTEXT_NAMESPACE "ErewhonBhurak"

using namespace ErewhonVisuals;

namespace
{
	/** Le modèle généré regarde vers +Y : on le tourne pour qu'il regarde vers +X. */
	constexpr float GBhurakMeshYaw = -90.f;
	/** Longueur d'une foulée (cm) : le corps ondule une fois par foulée. */
	constexpr float GBhurakStride = 150.f;

	const FString& BhurakLocalKey(const UObject* Context)
	{
		static const FString None;
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
		const AErewhonPlayerState* State = Local ? Local->GetPlayerState<AErewhonPlayerState>() : nullptr;
		return State ? State->GetPlayerKey() : None;
	}
}

FName ABhurak::CoatItem(uint8 InCoat)
{
	return InCoat == 1 ? FName(TEXT("bhurak_cream")) : InCoat == 2 ? FName(TEXT("bhurak_dark")) : ItemId;
}

int32 ABhurak::CoatOfItem(FName Item)
{
	for (int32 i = 0; i < CoatCount; ++i)
	{
		if (CoatItem(static_cast<uint8>(i)) == Item)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

UStaticMesh* ABhurak::Mesh(FTransform& OutFit, uint8 InCoat)
{
	UStaticMesh* Model = InCoat == 1 ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bhurak_cream"))
		: InCoat == 2 ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bhurak_dark")) : nullptr;
	if (!Model)
	{
		Model = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bhurak"));
	}
	if (Model)
	{
		OutFit = FitToHeight(Model, Height) * FTransform(FRotator(0.f, GBhurakMeshYaw, 0.f));
	}
	return Model;
}

ABhurak::ABhurak()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	AutoPossessAI = EAutoPossessAI::Disabled;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Root);
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetupAttachment(Root);
	Box->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Box->SetBoxExtent(FVector(HalfLength, HalfWidth, Height * 0.42f));
	Box->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.42f));
	Gear.SetNum(GearSlots);
	Bags.SetNum(GearSlots);
}

void ABhurak::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABhurak, OwnerKey);
	DOREPLIFETIME(ABhurak, OwnerName);
	DOREPLIFETIME(ABhurak, Coat);
	DOREPLIFETIME(ABhurak, bHarness);
	DOREPLIFETIME(ABhurak, Gear);
	DOREPLIFETIME(ABhurak, Bags);
	DOREPLIFETIME(ABhurak, Rider);
	DOREPLIFETIME(ABhurak, TowedCart);
}

void ABhurak::Setup(const FString& InOwnerKey, const FString& InOwnerName, uint8 InCoat)
{
	OwnerKey = InOwnerKey;
	OwnerName = InOwnerName;
	Coat = InCoat;
}

void ABhurak::BeginPlay()
{
	Super::BeginPlay();
	Yaw = GetActorRotation().Yaw;
	LastLocation = GetActorLocation();
	NextGrunt = GetWorld()->GetTimeSeconds() + FMath::FRandRange(8.f, 30.f);

	// Le corps : le modèle généré, à 1,80 m ; sa boîte de collision en découle.
	FTransform Fit;
	if (UStaticMesh* Model = Mesh(Fit, Coat))
	{
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(this, TEXT("Body"));
		Body->SetStaticMesh(Model);
		Body->SetupAttachment(BodyPivot);
		Body->SetRelativeTransform(Fit);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->RegisterComponent();
		AddInstanceComponent(Body);
		const FBox Bounds = Model->GetBoundingBox().TransformBy(Fit);
		HalfLength = FMath::Max(60.f, Bounds.GetExtent().X);
		HalfWidth = FMath::Max(30.f, Bounds.GetExtent().Y);
	}
	else
	{
		// Repli : un gros corps poilu sur quatre pattes, une tête.
		const FLinearColor Fur = FLinearColor::FromSRGBColor(FColor(150, 98, 56));
		AddShape(this, BodyPivot, EShape::Sphere, FVector(0, 0, 110), FVector(230, 110, 120), Fur, FRotator::ZeroRotator, false);
		AddShape(this, BodyPivot, EShape::Sphere, FVector(120, 0, 120), FVector(70, 60, 70), Fur, FRotator::ZeroRotator, false);
		for (const FVector2D Leg : { FVector2D(70, -35), FVector2D(70, 35), FVector2D(-70, -35), FVector2D(-70, 35) })
		{
			AddShape(this, BodyPivot, EShape::Cylinder, FVector(Leg, 30), FVector(24, 24, 60), Fur * 0.7f, FRotator::ZeroRotator, false);
		}
		HalfLength = 150.f;
		HalfWidth = 55.f;
	}
	Box->SetBoxExtent(FVector(HalfLength * 0.85f, HalfWidth * 0.9f, Height * 0.42f));
	ApplyGear();
}

FTransform ABhurak::GearTransform(int32 Slot) const
{
	// Les sacoches pendent aux flancs, sous le harnais ; celle du dos se pose derrière le garrot.
	if (Slot == BackSlot)
	{
		return FTransform(FRotator(0.f, 90.f, 0.f), FVector(-HalfLength * 0.15f, 0.f, SeatHeight - 12.f));
	}
	const float Side = Slot == 0 ? -1.f : 1.f;
	return FTransform(FRotator(0.f, Side > 0.f ? 0.f : 180.f, 0.f), FVector(-HalfLength * 0.1f, Side * (HalfWidth * 0.92f), Height * 0.42f));
}

FVector ABhurak::GetHitchPoint() const
{
	// Les brancards passent sous l'arrière-train : la charrette suit à bonne distance de la croupe.
	return GetActorLocation() - GetActorForwardVector() * (HalfLength * 0.55f);
}

void ABhurak::OnRep_Gear()
{
	ApplyGear();
}

void ABhurak::ApplyGear()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		OnGearChanged.Broadcast();
		return;
	}
	for (UStaticMeshComponent* Part : GearParts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	GearParts.Reset();
	if (bHarness)
	{
		// Le harnais : une sangle de poitrail, une sous-ventrière et une barre de dos, en cuir sombre.
		const FLinearColor Leather = FLinearColor::FromSRGBColor(FColor(78, 50, 32));
		// Des anneaux de cuir autour du corps (le gros de l'anneau se perd dans la toison, seul le bord dépasse).
		const float Middle = Height * 0.55f;
		GearParts.Add(AddShape(this, BodyPivot, EShape::Cylinder, FVector(HalfLength * 0.05f, 0.f, Middle), FVector(Height * 0.66f, HalfWidth * 2.06f, 12.f), Leather, FRotator(90.f, 0.f, 0.f), false));
		GearParts.Add(AddShape(this, BodyPivot, EShape::Cylinder, FVector(HalfLength * 0.5f, 0.f, Middle + 8.f), FVector(Height * 0.6f, HalfWidth * 1.95f, 10.f), Leather, FRotator(90.f, 0.f, 0.f), false));
		GearParts.Add(AddShape(this, BodyPivot, EShape::Cube, FVector(-HalfLength * 0.1f, 0.f, SeatHeight - 4.f), FVector(HalfLength * 1.0f, 14.f, 5.f), Leather, FRotator::ZeroRotator, false));
	}
	if (HasSaddle())
	{
		if (UStaticMesh* Saddle = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_saddle")))
		{
			UStaticMeshComponent* Part = AddGeneratedMesh(this, BodyPivot, Saddle, FVector(HalfLength * 0.05f, 0.f, SeatHeight - 16.f), 40.f, 0.f, false);
			Part->SetRelativeScale3D(Part->GetRelativeScale3D() * 1.15f);
			GearParts.Add(Part);
		}
		else
		{
			GearParts.Add(AddShape(this, BodyPivot, EShape::Cube, FVector(HalfLength * 0.05f, 0.f, SeatHeight - 6.f), FVector(60.f, 70.f, 16.f), FLinearColor::FromSRGBColor(FColor(110, 66, 40)), FRotator::ZeroRotator, false));
		}
	}
	OnGearChanged.Broadcast();
}

void ABhurak::SetHarness(bool bOn)
{
	bHarness = bOn;
	ApplyGear();
	ForceNetUpdate();
}

void ABhurak::SetGear(int32 Slot, FName Item, AStorageActor* Bag)
{
	if (!Gear.IsValidIndex(Slot))
	{
		return;
	}
	Gear[Slot] = Item;
	Bags[Slot] = Bag;
	if (Bag)
	{
		Bag->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		Bag->SetActorRelativeTransform(GearTransform(Slot));
	}
	if (APawn* Mounted = Rider)
	{
		ApplyRiderCollision(Mounted, true);
	}
	ApplyGear();
	ForceNetUpdate();
}

void ABhurak::SetRider(APawn* Pawn)
{
	if (Rider && Rider != Pawn)
	{
		ApplyRiderCollision(Rider, false);
	}
	Rider = Pawn;
	OnRep_Rider();
	ForceNetUpdate();
}

void ABhurak::SetTowedCart(ACart* Cart)
{
	if (Rider && TowedCart && TowedCart != Cart)
	{
		ApplyRiderCollision(Rider, false);
	}
	TowedCart = Cart;
	if (Rider)
	{
		ApplyRiderCollision(Rider, true);
	}
	ForceNetUpdate();
}

void ABhurak::OnRep_Rider()
{
	if (AppliedRider.IsValid() && AppliedRider.Get() != Rider)
	{
		ApplyRiderCollision(AppliedRider.Get(), false);
	}
	AppliedRider = Rider;
	if (Rider)
	{
		ApplyRiderCollision(Rider, true);
		Yaw = GetActorRotation().Yaw;
	}
}

void ABhurak::ApplyRiderCollision(APawn* Pawn, bool bIgnore)
{
	if (!Pawn)
	{
		return;
	}
	TArray<AActor*> Ignored;
	GetAttachedActors(Ignored, true, true);
	Ignored.Add(this);
	if (TowedCart)
	{
		Ignored.Add(TowedCart);
		TowedCart->GetAttachedActors(Ignored, false, true);
	}
	for (AActor* Actor : Ignored)
	{
		if (bIgnore)
		{
			Pawn->MoveIgnoreActorAdd(Actor);
		}
		else
		{
			Pawn->MoveIgnoreActorRemove(Actor);
		}
	}
}

FText ABhurak::GetInteractionLabel() const
{
	if (!IsOwnedBy(BhurakLocalKey(this)))
	{
		return FText::Format(LOCTEXT("Theirs", "Bhurak de {0}"), FText::FromString(OwnerName));
	}
	if (HasSaddle())
	{
		return TowedCart ? LOCTEXT("MineSaddledCart", "Votre bhurak (attelé)    [F] Monter    [G] Dételer")
			: LOCTEXT("MineSaddled", "Votre bhurak    [F] Monter");
	}
	return TowedCart ? LOCTEXT("MineCart", "Votre bhurak (attelé)    [G] Dételer") : LOCTEXT("Mine", "Votre bhurak");
}

void ABhurak::Interact(AErewhonPlayerController* User)
{
	if (User)
	{
		User->OpenBhurak(this);
	}
}

void ABhurak::PostNetReceiveLocationAndRotation()
{
	// Monté, chaque machine le place elle-même sous son cavalier, sans à-coups réseau.
	if (!Rider)
	{
		Super::PostNetReceiveLocationAndRotation();
	}
}

float ABhurak::GroundAt(const FVector& World) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonBhurakGround), false, this);
	TArray<AActor*> Ignored;
	GetAttachedActors(Ignored, true, true);
	Params.AddIgnoredActors(Ignored);
	if (Rider)
	{
		Params.AddIgnoredActor(Rider);
	}
	if (TowedCart)
	{
		Params.AddIgnoredActor(TowedCart);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, World + FVector(0, 0, 150), World - FVector(0, 0, 400), ECC_WorldStatic, Params))
	{
		return Hit.ImpactPoint.Z;
	}
	return World.Z;
}

void ABhurak::FollowRider(float DeltaSeconds)
{
	// Sous le cavalier ; il se tourne vers où il va (à reculons, il recule sans faire demi-tour).
	const ACharacter* Character = Cast<ACharacter>(Rider);
	const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
	const FVector Feet = Rider->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	const FVector Velocity = Rider->GetVelocity();
	if (Velocity.Size2D() > 40.f)
	{
		float Heading = Velocity.Rotation().Yaw;
		if (FVector::DotProduct(Velocity.GetSafeNormal2D(), FRotator(0.f, Yaw, 0.f).Vector()) < -0.3f)
		{
			Heading += 180.f;
		}
		Yaw = FMath::FixedTurn(Yaw, Heading, 200.f * DeltaSeconds);
	}
	SetActorLocationAndRotation(FVector(Feet.X, Feet.Y, GroundAt(Feet)), FRotator(0.f, Yaw, 0.f));
}

void ABhurak::Animate(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f)
	{
		return;
	}
	const float Moved = FVector::Dist2D(GetActorLocation(), LastLocation);
	LastLocation = GetActorLocation();
	Speed = FMath::FInterpTo(Speed, Moved < 300.f ? Moved / DeltaSeconds : 0.f, DeltaSeconds, 5.f);
	const float Gait = FMath::Clamp(Speed / 250.f, 0.f, 1.f);
	WalkPhase = FMath::Fmod(WalkPhase + Moved / GBhurakStride * UE_TWO_PI, UE_TWO_PI);
	// Au pas, le dos tangue et rebondit ; à l'arrêt, il respire.
	const float Time = GetWorld()->GetTimeSeconds();
	const float Bob = FMath::Abs(FMath::Sin(WalkPhase)) * 5.f * Gait;
	const float Breath = FMath::Sin(Time * 1.4f) * 0.008f * (1.f - Gait);
	// À l'arrêt, une attente : brouter (le nez vers le sol), regarder d'un côté, s'ébrouer, changer d'appui.
	if (Gait > 0.1f || Time >= IdleEnd)
	{
		const float Pick = FMath::FRand();
		Idle = Gait > 0.1f ? EIdle::Stand : Pick < 0.3f ? EIdle::Stand : Pick < 0.55f ? EIdle::Graze : Pick < 0.67f ? EIdle::LookLeft
			: Pick < 0.79f ? EIdle::LookRight : Pick < 0.88f ? EIdle::Shake : EIdle::ShiftWeight;
		IdleStart = Time;
		IdleEnd = Time + (Idle == EIdle::Graze ? FMath::FRandRange(6.f, 14.f) : Idle == EIdle::Shake ? 1.4f : FMath::FRandRange(3.f, 7.f));
	}
	const float IdleTime = static_cast<float>(Time - IdleStart);
	FRotator Wanted = FRotator::ZeroRotator;
	switch (Idle)
	{
	case EIdle::Graze:       Wanted.Pitch = -7.f + FMath::Sin(IdleTime * 1.7f) * 1.2f; break;   // la tête basse, qui mâchonne
	case EIdle::LookLeft:    Wanted.Yaw = -12.f; Wanted.Pitch = 1.5f; break;
	case EIdle::LookRight:   Wanted.Yaw = 12.f; Wanted.Pitch = 1.5f; break;
	case EIdle::Shake:       Wanted.Roll = FMath::Sin(IdleTime * 26.f) * 5.f * (1.f - IdleTime / 1.4f); break;
	case EIdle::ShiftWeight: Wanted.Roll = 2.5f; break;
	default: break;
	}
	IdlePose = Idle == EIdle::Shake ? FRotator(IdlePose.Pitch + (Wanted.Pitch - IdlePose.Pitch) * 0.1f, IdlePose.Yaw * 0.95f, Wanted.Roll)
		: FMath::RInterpTo(IdlePose, Wanted, DeltaSeconds, 1.8f);
	// Le corps pivote autour de l'arrière-train : quand il broute, c'est l'avant qui descend.
	const FRotator Posed = FRotator(FMath::Sin(WalkPhase) * 1.6f * Gait + IdlePose.Pitch, IdlePose.Yaw, FMath::Sin(WalkPhase * 0.5f) * 1.4f * Gait + IdlePose.Roll);
	const FVector Pivot(-HalfLength * 0.6f, 0.f, 0.f);
	const FVector Shift = Pivot - Posed.RotateVector(Pivot);
	BodyPivot->SetRelativeLocationAndRotation(FVector(0.f, 0.f, Bob) + Shift, Posed);
	BodyPivot->SetRelativeScale3D(FVector(1.f, 1.f + Breath * 0.6f, 1.f + Breath));

	// De temps en temps, un grognement tranquille.
	if (Time >= NextGrunt)
	{
		NextGrunt = Time + FMath::FRandRange(25.f, 60.f);
		ErewhonSound::PlayAt(this, ErewhonSound::Load(TEXT("bhurak_low")), GetActorLocation() + GetActorForwardVector() * HalfLength + FVector(0, 0, 120.f),
			0.6f, 300.f, 2500.f);
	}
}

void ABhurak::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Rider)
	{
		FollowRider(DeltaSeconds);
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		Animate(DeltaSeconds);
	}
}

#undef LOCTEXT_NAMESPACE
