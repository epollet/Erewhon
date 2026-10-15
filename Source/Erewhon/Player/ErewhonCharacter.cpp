#include "Player/ErewhonCharacter.h"
#include "World/Forestry.h"
#include "World/FelledTree.h"
#include "Components/PointLightComponent.h"
#include "EngineUtils.h"

#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Inventory/BackpackComponent.h"
#include "World/Backpack.h"
#include "World/Village.h"
#include "World/Storage.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "Player/Interactable.h"
#include "World/ErewhonVisuals.h"
#include "World/ErewhonSound.h"
#include "World/SpawnLayout.h"
#include "World/Apothecary.h"
#include "World/Farm.h"
#include "World/Bhurak.h"
#include "World/Sector.h"
#include "World/SectorManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "ErewhonCharacter"

using namespace ErewhonVisuals;

namespace
{
	constexpr float GSwingDuration = 0.35f;

	/** La rotation qui amène la direction A du modèle sur Aw, et B (sa part perpendiculaire à A) sur Bw. */
	FQuat AlignPair(const FVector& A, const FVector& B, const FVector& Aw, const FVector& Bw)
	{
		return FQuat(FRotationMatrix::MakeFromXY(Aw, Bw)) * FQuat(FRotationMatrix::MakeFromXY(A, B)).Inverse();
	}

	/**
	 * La faux tenue, vue de soi : le manche (du bas à droite du modèle vers le haut à gauche) part des mains
	 * vers l'avant et vers le bas ; la lame (qui pend du haut du manche) se couche près du sol, vers la gauche.
	 */
	const FVector GScytheSnath(-0.59f, 0.f, 0.81f);
	const FVector GScytheBlade(-0.2f, 0.f, -0.98f);
	const FVector GScytheSnathView(0.97f, 0.f, -0.24f);
	const FVector GScytheBladeView(-0.6f, -0.75f, -0.28f);
	const FVector GScytheHand(40.f, 18.f, -24.f);
	constexpr double GPendingHarvestSeconds = 2.0;   // le geste (une seconde à la main), plus la latence
	const FVector GHeldFirstPersonOffset(38.f, 24.f, -22.f);
	const FRotator GHeldFirstPersonRotation(-20.f, 0.f, 10.f);
	const FVector GHeldThirdPersonOffset(22.f, 28.f, 0.f);

	/** Rotation du modèle animé pour qu'il regarde vers +X (l'avant du personnage). */
	constexpr float GBodyMeshYaw = -90.f;
	/** Les animations du joueur (importées en A_player_<nom>) et leur vitesse au sol nominale (cm/s). */
	const TCHAR* GBodyAnimations[] = { TEXT("idle"), TEXT("walk"), TEXT("run"), TEXT("jump"), TEXT("sit"), TEXT("crouch"),
		TEXT("carry"), TEXT("push"), TEXT("pickup") };
}

AErewhonCharacter::AErewhonCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// 1,76 m au total, regard à environ 1,55 m.
	GetCapsuleComponent()->InitCapsuleSize(32.f, 88.f);
	bUseControllerRotationYaw = true;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = UErewhonSettings::Get().WalkSpeedMetersPerSecond * 100.f;
	Movement->JumpZVelocity = 450.f;   // ≈ 1 m de saut
	Movement->AirControl = 0.3f;
	Movement->bOrientRotationToMovement = false;
	Movement->NavAgentProps.bCanCrouch = true;
	Movement->SetCrouchedHalfHeight(55.f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 66.f));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(80.f);

	const auto MakeHeld = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Held = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Held->SetupAttachment(Parent);
		Held->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Held->SetVisibility(false);
		return Held;
	};
	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(GetCapsuleComponent());
	CameraArm->SetRelativeLocation(FVector(0.f, 0.f, 62.f));
	CameraArm->TargetArmLength = 320.f;
	CameraArm->SocketOffset = FVector(0.f, 0.f, 20.f);
	CameraArm->bUsePawnControlRotation = true;
	CameraArm->bDoCollisionTest = true;
	ThirdCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdCamera"));
	ThirdCamera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	ThirdCamera->SetFieldOfView(80.f);
	ThirdCamera->SetAutoActivate(false);

	HeldFirstPerson = MakeHeld(TEXT("HeldFirstPerson"), Camera);
	HeldFirstPerson->SetRelativeLocationAndRotation(GHeldFirstPersonOffset, GHeldFirstPersonRotation);
	HeldFirstPerson->SetOnlyOwnerSee(true);
	HeldFirstPerson->SetCastShadow(false);

	HeldThirdPerson = MakeHeld(TEXT("HeldThirdPerson"), GetCapsuleComponent());
	HeldThirdPerson->SetRelativeLocation(GHeldThirdPersonOffset);
	HeldThirdPerson->SetOwnerNoSee(true);
}

void AErewhonCharacter::BeginPlay()
{
	Super::BeginPlay();
	BuildBody();
	BuildAnimatedBody();
	BuildWornBackpack();
	BindAppearance();
	// Le PlayerState a pu être lié avant que le corps n'existe : on applique son état maintenant.
	ApplyAppearance();
	ApplyBackpack();
}

void AErewhonCharacter::BuildAnimatedBody()
{
	USkeletalMesh* Model = LoadGenerated<USkeletalMesh>(TEXT("Characters/player"), TEXT("SK_player"));
	if (!Model)
	{
		return;
	}
	AnimatedBody = NewObject<USkeletalMeshComponent>(this, TEXT("AnimatedBody"));
	AnimatedBody->SetupAttachment(GetCapsuleComponent());
	// Les pieds au bas de la capsule, le regard vers l'avant.
	AnimatedBody->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), FRotator(0.f, GBodyMeshYaw, 0.f));
	AnimatedBody->SetSkeletalMesh(Model);
	AnimatedBody->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	AnimatedBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AnimatedBody->RegisterComponent();
	AddInstanceComponent(AnimatedBody);
	for (const TCHAR* Name : GBodyAnimations)
	{
		if (UAnimSequence* Sequence = LoadGenerated<UAnimSequence>(TEXT("Characters/player"), FString::Printf(TEXT("A_player_%s"), Name)))
		{
			BodyAnimations.Add(Name, Sequence);
		}
	}
	// La silhouette provisoire s'efface devant le vrai personnage.
	for (UStaticMeshComponent* Part : { Legs.Get(), Torso.Get(), Head.Get() })
	{
		if (Part)
		{
			Part->SetVisibility(false);
		}
	}
	// L'objet tenu suit la main droite du squelette.
	for (const FName Bone : { FName(TEXT("RightHand")), FName(TEXT("hand_r")), FName(TEXT("Hand_R")), FName(TEXT("mixamorig:RightHand")) })
	{
		if (AnimatedBody->GetBoneIndex(Bone) != INDEX_NONE)
		{
			HeldThirdPerson->AttachToComponent(AnimatedBody, FAttachmentTransformRules::KeepRelativeTransform, Bone);
			HeldThirdPersonBase = FVector(0.f, 4.f, 0.f);
			break;
		}
	}
	// Le bassin, pour garder sur place les animations qui avancent (porter, pousser, accroupi).
	for (int32 Bone = 0; Bone < AnimatedBody->GetNumBones(); ++Bone)
	{
		const FString Name = AnimatedBody->GetBoneName(Bone).ToString();
		if (Name.Contains(TEXT("hips"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("pelvis"), ESearchCase::IgnoreCase))
		{
			HipsBone = AnimatedBody->GetBoneName(Bone);
			break;
		}
	}
	PlayBodyAnimation(TEXT("idle"), true);
}

void AErewhonCharacter::KeepBodyInPlace()
{
	if (!AnimatedBody || HipsBone.IsNone())
	{
		return;
	}
	// Assis ou en l'air, le bassin se déplace pour de bon ; sinon il reste à l'aplomb de la capsule.
	FVector Offset = FVector::ZeroVector;
	if ((CurrentAnimation != TEXT("sit") || Mount) && CurrentAnimation != TEXT("jump"))
	{
		const FVector Hips = AnimatedBody->GetSocketTransform(HipsBone, RTS_Component).GetLocation();
		Offset = AnimatedBody->GetRelativeRotation().RotateVector(FVector(Hips.X, Hips.Y, 0.f));
	}
	const float Rise = Mount ? ABhurak::RideRise : 0.f;
	AnimatedBody->SetRelativeLocation(FVector(0.f, 0.f, Rise - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) - Offset);
}

void AErewhonCharacter::PlayBodyAnimation(FName Name, bool bLoop, float PlayRate)
{
	if (!AnimatedBody)
	{
		return;
	}
	if (Name != CurrentAnimation)
	{
		const TObjectPtr<UAnimSequence>* Sequence = BodyAnimations.Find(Name);
		if (!Sequence || !*Sequence)
		{
			return;
		}
		CurrentAnimation = Name;
		AnimatedBody->PlayAnimation(Sequence->Get(), bLoop);
	}
	AnimatedBody->SetPlayRate(PlayRate);
}

void AErewhonCharacter::UpdateAnimation(float DeltaSeconds)
{
	// Le geste du moment, déduit de ce que tout le monde voit (vitesse, banc, charrette, conteneur porté).
	if (!AnimatedBody)
	{
		return;
	}
	KeepBodyInPlace();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();
	if (Mount)
	{
		// En selle : assis, tourné comme sa monture.
		PlayBodyAnimation(TEXT("sit"), true);
		AnimatedBody->SetWorldRotation(FRotator(0.f, Mount->GetActorRotation().Yaw + GBodyMeshYaw, 0.f));
		return;
	}
	if (GetWorld()->GetTimeSeconds() < OneShotUntil)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	// -ErewhonTestAnim=<nom> : captures, une animation imposée.
	FString Forced;
	if (FParse::Value(FCommandLine::Get(), TEXT("ErewhonTestAnim="), Forced))
	{
		PlayBodyAnimation(*Forced, true);
		return;
	}
#endif
	if (IsSeated())
	{
		PlayBodyAnimation(TEXT("sit"), true);
	}
	else if (Movement->IsFalling())
	{
		PlayBodyAnimation(TEXT("jump"), false);
	}
	else if (DrivenCart && Speed > 20.f && FVector::DotProduct(GetVelocity(), DrivenCart->GetActorLocation() - GetActorLocation()) > 0.f)
	{
		PlayBodyAnimation(TEXT("push"), true, FMath::Clamp(Speed / 200.f, 0.5f, 1.6f));   // on recule vers elle : on la pousse
	}
	else if (Carried && Speed > 20.f)
	{
		PlayBodyAnimation(TEXT("carry"), true, FMath::Clamp(Speed / 200.f, 0.5f, 1.6f));
	}
	else if (bIsCrouched)
	{
		PlayBodyAnimation(TEXT("crouch"), true, Speed > 20.f ? FMath::Clamp(Speed / 150.f, 0.5f, 1.5f) : 0.f);
	}
	else if (Speed > 520.f)
	{
		PlayBodyAnimation(TEXT("run"), true, FMath::Clamp(Speed / 600.f, 0.6f, 1.5f));
	}
	else if (Speed > 20.f)
	{
		PlayBodyAnimation(TEXT("walk"), true, FMath::Clamp(Speed / 300.f, 0.5f, 1.6f));
	}
	else
	{
		PlayBodyAnimation(TEXT("idle"), true);
	}
}

void AErewhonCharacter::BuildBody()
{
	// Silhouette provisoire, vue par les autres joueurs uniquement.
	USceneComponent* Capsule = GetCapsuleComponent();
	Legs = AddShape(this, Capsule, EShape::Cylinder, FVector(0, 0, -50), FVector(34, 28, 76), Palette::DefaultShorts, FRotator::ZeroRotator, false);
	Torso = AddShape(this, Capsule, EShape::Cylinder, FVector(0, 0, 18), FVector(42, 30, 62), Palette::Skin, FRotator::ZeroRotator, false);
	Head = AddShape(this, Capsule, EShape::Sphere, FVector(0, 0, 66), FVector(30, 30, 32), Palette::Skin, FRotator::ZeroRotator, false);
	for (UStaticMeshComponent* Part : { Legs.Get(), Torso.Get(), Head.Get() })
	{
		Part->SetOwnerNoSee(true);
	}
}

void AErewhonCharacter::BuildWornBackpack()
{
	// Dans le dos, poches vers l'arrière, le bas du sac à la taille.
	WornBackpack = NewObject<USceneComponent>(this, TEXT("WornBackpack"));
	WornBackpack->SetupAttachment(GetCapsuleComponent());
	WornBackpack->SetRelativeLocationAndRotation(FVector(-27.f, 0.f, -12.f), FRotator(0.f, 180.f, 0.f));
	WornBackpack->RegisterComponent();
	AddInstanceComponent(WornBackpack);
	for (UStaticMeshComponent* Holder : ErewhonBackpack::Build(this, WornBackpack, false))
	{
		WornTools.Add(Holder);
	}
	WornBackpack->SetVisibility(false, true);
}

void AErewhonCharacter::AttachBackpackToSpine()
{
	// Avec le corps animé, le sac suit le haut du dos (il bouge avec la marche, la course, le saut).
	// On l'y accroche une fois la pose de repos calculée : il garde alors sa place dans le dos.
	if (bBackpackOnSpine || !AnimatedBody || !WornBackpack || GetGameTimeSinceCreation() < 0.5f)
	{
		return;
	}
	bBackpackOnSpine = true;
	TArray<FName> Bones;
	AnimatedBody->GetBoneNames(Bones);
	FName Spine;
	for (const FName& Bone : Bones)
	{
		const FString Name = Bone.ToString();
		if (Name.Contains(TEXT("Spine"), ESearchCase::IgnoreCase) || Name.Contains(TEXT("Chest"), ESearchCase::IgnoreCase))
		{
			Spine = Bone;   // le dernier trouvé est le plus haut
		}
	}
	if (!Spine.IsNone())
	{
		const FTransform Wanted = FTransform(FRotator(0.f, 180.f, 0.f), FVector(-27.f, 0.f, -12.f)) * GetCapsuleComponent()->GetComponentTransform();
		WornBackpack->AttachToComponent(AnimatedBody, FAttachmentTransformRules::KeepWorldTransform, Spine);
		WornBackpack->SetWorldTransform(Wanted);
	}
}

void AErewhonCharacter::ApplyBackpack()
{
	const UBackpackComponent* Bag = BoundBackpack.Get();
	const bool bWorn = Bag && Bag->IsWorn();
	if (!WornBackpack)
	{
		return;
	}
	WornBackpack->SetVisibility(bWorn, true);
	if (bWorn)
	{
		TArray<UStaticMeshComponent*> Holders;
		for (UStaticMeshComponent* Holder : WornTools)
		{
			Holders.Add(Holder);
		}
		ErewhonBackpack::SetTools(Holders, Bag->GetSideTools());
	}
	ApplyView();
}

void AErewhonCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AErewhonCharacter, SeatIndex);
	DOREPLIFETIME(AErewhonCharacter, Carried);
	DOREPLIFETIME(AErewhonCharacter, DrivenCart);
	DOREPLIFETIME(AErewhonCharacter, Mount);
	DOREPLIFETIME(AErewhonCharacter, PotionUntil);
}

void AErewhonCharacter::ServerSit_Implementation(int32 Seat)
{
	const TArray<FTransform>& Seats = ErewhonVillage::BenchSeats();
	if (!Seats.IsValidIndex(Seat) || FVector::Dist2D(GetActorLocation(), Seats[Seat].GetLocation()) > 350.f)
	{
		return;
	}
	SeatIndex = Seat;
	ApplySeat();
}

void AErewhonCharacter::ServerStand_Implementation()
{
	if (SeatIndex == INDEX_NONE)
	{
		return;
	}
	const FTransform Seat = ErewhonVillage::BenchSeats()[SeatIndex];
	SeatIndex = INDEX_NONE;
	ApplySeat();
	// On se relève devant le banc.
	SetActorLocation(Seat.GetLocation() + Seat.GetRotation().GetForwardVector() * 70.f + FVector(0.f, 0.f, 60.f));
}

void AErewhonCharacter::OnRep_Seat()
{
	ApplySeat();
}

void AErewhonCharacter::ApplySeat()
{
	// Assis : immobile, le bassin sur le siège, les jambes tendues vers l'avant.
	bStandRequested = false;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (SeatIndex != INDEX_NONE && ErewhonVillage::BenchSeats().IsValidIndex(SeatIndex))
	{
		const FTransform Seat = ErewhonVillage::BenchSeats()[SeatIndex];
		Movement->DisableMovement();
		const float SeatedZ = AnimatedBody ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 45.f : 14.f;
		SetActorLocationAndRotation(Seat.GetLocation() + FVector(0.f, 0.f, SeatedZ), Seat.Rotator());
		if (Legs && !AnimatedBody)
		{
			Legs->SetRelativeLocationAndRotation(FVector(26.f, 0.f, -18.f), FRotator(-90.f, 0.f, 0.f));
		}
		if (IsLocallyControlled() && Controller)
		{
			Controller->SetControlRotation(Seat.Rotator());
		}
		return;
	}
	if (Movement->MovementMode == MOVE_None)
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	if (Legs)
	{
		Legs->SetRelativeLocationAndRotation(FVector(0, 0, -50), FRotator::ZeroRotator);
	}
}

void AErewhonCharacter::CycleView()
{
	ViewMode = ViewMode == EViewMode::FirstPerson ? EViewMode::Behind
		: ViewMode == EViewMode::Behind ? EViewMode::Front : EViewMode::FirstPerson;
	ApplyView();
}

void AErewhonCharacter::ApplyView()
{
	const bool bFirst = ViewMode == EViewMode::FirstPerson;
	Camera->SetActive(bFirst);
	ThirdCamera->SetActive(!bFirst);
	if (ViewMode == EViewMode::Front)
	{
		// Le bras pointe vers l'arrière du personnage : la caméra se place devant lui et le regarde.
		CameraArm->bUsePawnControlRotation = false;
		CameraArm->SetRelativeRotation(FRotator(-12.f, 180.f, 0.f));
		CameraArm->TargetArmLength = 260.f;
	}
	else
	{
		CameraArm->bUsePawnControlRotation = true;
		CameraArm->SetRelativeRotation(FRotator::ZeroRotator);
		CameraArm->TargetArmLength = 320.f;
	}

	// À la première personne, on ne voit pas son propre corps ; sinon si.
	TArray<USceneComponent*> SelfParts = { Torso.Get(), Legs.Get(), Head.Get(), HeldThirdPerson.Get(), AnimatedBody.Get() };
	for (UStaticMeshComponent* Part : StowedParts)
	{
		SelfParts.Add(Part);
	}
	for (UStaticMeshComponent* Strap : HarnessStraps)
	{
		SelfParts.Add(Strap);
	}
	if (WornBackpack)
	{
		TArray<USceneComponent*> BagParts;
		WornBackpack->GetChildrenComponents(true, BagParts);
		SelfParts.Append(BagParts);
	}
	for (USceneComponent* Part : SelfParts)
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part))
		{
			Primitive->SetOwnerNoSee(bFirst);
		}
	}
	HeldFirstPerson->SetHiddenInGame(!bFirst);
}

void AErewhonCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	BindAppearance();
}

void AErewhonCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindAppearance();
}

void AErewhonCharacter::BindAppearance()
{
	AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	if (UBackpackComponent* Bag = State ? State->GetBackpack() : nullptr; Bag && BoundBackpack != Bag)
	{
		if (UBackpackComponent* Previous = BoundBackpack.Get())
		{
			Previous->OnBackpackChanged.Remove(BackpackHandle);
		}
		BoundBackpack = Bag;
		BackpackHandle = Bag->OnBackpackChanged.AddUObject(this, &AErewhonCharacter::ApplyBackpack);
		ApplyBackpack();
	}
	UInventoryComponent* Inventory = State ? State->GetInventory() : nullptr;
	if (!Inventory || BoundInventory == Inventory)
	{
		return;
	}
	if (UInventoryComponent* Previous = BoundInventory.Get())
	{
		Previous->OnInventoryChanged.Remove(AppearanceHandle);
	}
	BoundInventory = Inventory;
	AppearanceHandle = Inventory->OnInventoryChanged.AddUObject(this, &AErewhonCharacter::ApplyAppearance);
	if (IsLocallyControlled())
	{
		Inventory->OnInventoryMessage.AddWeakLambda(this, [this](const FText& Message)
		{
			if (AErewhonPlayerController* PC = GetController<AErewhonPlayerController>())
			{
				PC->ShowToast(Message);
			}
		});
	}
	ApplyAppearance();
}

void AErewhonCharacter::ApplyAppearance()
{
	const UInventoryComponent* Inventory = BoundInventory.Get();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!Inventory || !Items)
	{
		return;
	}
	const FInventoryAppearance& Look = Inventory->GetAppearance();

	if (Torso && Legs)
	{
		const FItemDef* TorsoDef = Items->Find(Look.TorsoItem);
		const FItemDef* LegsDef = Items->Find(Look.LegsItem);
		Torso->SetMaterial(0, MakeColorMaterial(Torso, TorsoDef ? TorsoDef->Color : Palette::Skin));
		Legs->SetMaterial(0, MakeColorMaterial(Legs, LegsDef ? LegsDef->Color : Palette::DefaultShorts));
	}

	ApplyStowed();
	const FItemDef* Hand = Items->Find(Look.HandItem);
	for (UStaticMeshComponent* Held : { HeldFirstPerson.Get(), HeldThirdPerson.Get() })
	{
		// Ce qui se pose (charrette, cagette, tonneau) ne se tient pas : on en voit l'aperçu au sol.
		Held->SetVisibility(Hand != nullptr && !Hand->IsPlaceable());
		if (!Hand || Hand->IsPlaceable())
		{
			continue;
		}
		if (UStaticMesh* HandMesh = Cast<UStaticMesh>(Hand->Mesh.TryLoad()))
		{
			// Modèle généré : ramené à ~45 cm, la base (le manche) au creux de la main.
			const float HandHeight = Hand->bHoldable ? 24.f : Hand->Id == ErewhonFarm::Scythe ? 80.f : Hand->Id == ErewhonFarm::Spade ? 70.f : 45.f;
			const FTransform Fit = FitToHeight(HandMesh, HandHeight);
			const bool bFirstScythe = Held == HeldFirstPerson.Get() && Hand->Id == ErewhonFarm::Scythe;
			const FVector Base = bFirstScythe ? GScytheHand : Held == HeldFirstPerson.Get() ? GHeldFirstPersonOffset : (AnimatedBody ? HeldThirdPersonBase : GHeldThirdPersonOffset);
			// La faux se tient lame en bas ; la bêche, lame vers l'avant.
			const FQuat Turn = Hand->Id == ErewhonFarm::Scythe ? FQuat(FRotator(Held == HeldFirstPerson.Get() ? -125.f : 0.f, 0.f, 0.f)) : Hand->Id == ErewhonFarm::Spade ? FQuat(FRotator(0.f, 135.f, 0.f)) : FQuat::Identity;
			// Vu de soi, la faux : posée d'après la forme de son modèle, tenue par le bout du manche.
			FVector Offset = Fit.GetLocation();
			if (bFirstScythe)
			{
				const FBox Bounds = HandMesh->GetBoundingBox();
				Offset -= FVector((Bounds.Max.X - Bounds.GetCenter().X) * 0.92f * Fit.GetScale3D().X, 0.f, 0.f);
				HeldFirstRotation = AlignPair(GScytheSnath, GScytheBlade, GScytheSnathView, GScytheBladeView).Rotator();
				Held->SetRelativeRotation(HeldFirstRotation);
			}
			else if (Held == HeldFirstPerson.Get())
			{
				HeldFirstRotation = (FQuat(GHeldFirstPersonRotation) * Turn).Rotator();
				Held->SetRelativeRotation(HeldFirstRotation);
			}
			else
			{
				Held->SetRelativeRotation(Turn.Rotator());
			}
			Held->SetStaticMesh(HandMesh);
			Held->SetRelativeLocation(Base + Held->GetRelativeRotation().RotateVector(Offset));
			if (Held == HeldFirstPerson.Get())
			{
				HeldFirstLocation = Held->GetRelativeLocation();
				HeldFirstHand = Base;
				HeldFirstPivotOffset = Offset;
			}
			Held->SetRelativeScale3D(Fit.GetScale3D());
			Held->EmptyOverrideMaterials();
		}
		else
		{
			// Placeholder : un manche, de la couleur de l'outil.
			Held->SetStaticMesh(GetShape(EShape::Cube));
			Held->SetRelativeLocation(Held == HeldFirstPerson.Get() ? GHeldFirstPersonOffset : GHeldThirdPersonOffset);
			Held->SetRelativeScale3D(FVector(0.04f, 0.04f, 0.45f));
			Held->SetMaterial(0, MakeColorMaterial(Held, Hand->Color));
		}
	}
}

// ── Entrées ──────────────────────────────────────────────────────────────────

void AErewhonCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AErewhonPlayerController* PC = GetController<AErewhonPlayerController>();
	if (!Input || !PC)
	{
		return;
	}
	const FErewhonInputActions& Actions = PC->GetInputActions();
	Input->BindAction(Actions.Move, ETriggerEvent::Triggered, this, &AErewhonCharacter::Move);
	Input->BindAction(Actions.Look, ETriggerEvent::Triggered, this, &AErewhonCharacter::Look);
	Input->BindAction(Actions.Jump, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(Actions.Jump, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(Actions.Run, ETriggerEvent::Started, this, &AErewhonCharacter::StartRun);
	Input->BindAction(Actions.Run, ETriggerEvent::Completed, this, &AErewhonCharacter::StopRun);
	Input->BindAction(Actions.Primary, ETriggerEvent::Started, this, &AErewhonCharacter::Primary);
	Input->BindAction(Actions.Secondary, ETriggerEvent::Started, this, &AErewhonCharacter::Secondary);
	Input->BindAction(Actions.Use, ETriggerEvent::Started, this, &AErewhonCharacter::Use);
	Input->BindAction(Actions.Crouch, ETriggerEvent::Started, this, &AErewhonCharacter::StartCrouch);
	Input->BindAction(Actions.Crouch, ETriggerEvent::Completed, this, &AErewhonCharacter::StopCrouch);
	Input->BindAction(Actions.Inventory, ETriggerEvent::Started, this, &AErewhonCharacter::ToggleInventory);
	Input->BindAction(Actions.Bag, ETriggerEvent::Started, this, &AErewhonCharacter::BagAction);
	Input->BindAction(Actions.Stash, ETriggerEvent::Started, this, &AErewhonCharacter::StashAction);
	Input->BindAction(Actions.View, ETriggerEvent::Started, this, &AErewhonCharacter::CycleView);
	Input->BindAction(Actions.Hotbar1, ETriggerEvent::Started, this, &AErewhonCharacter::SelectHotbar1);
	Input->BindAction(Actions.Hotbar2, ETriggerEvent::Started, this, &AErewhonCharacter::SelectHotbar2);
	Input->BindAction(Actions.Hotbar3, ETriggerEvent::Started, this, &AErewhonCharacter::SelectHotbar3);
	Input->BindAction(Actions.HotbarScroll, ETriggerEvent::Triggered, this, &AErewhonCharacter::ScrollHotbar);
	Input->BindAction(Actions.CloseMenu, ETriggerEvent::Started, PC, &AErewhonPlayerController::HandleEscape);
}

bool AErewhonCharacter::IsMenuOpen() const
{
	const AErewhonPlayerController* PC = GetController<AErewhonPlayerController>();
	return PC && PC->IsMenuOpen();
}

void AErewhonCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (IsSeated())
	{
		// Un pas en avant suffit à se relever.
		if (!bStandRequested && !Axis.IsNearlyZero() && !IsMenuOpen())
		{
			bStandRequested = true;
			ServerStand();
		}
		return;
	}
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
}

void AErewhonCharacter::Look(const FInputActionValue& Value)
{
	if (IsMenuOpen())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void AErewhonCharacter::StartRun()
{
	ApplyRunning(true);
	ServerSetRunning(true);
}

void AErewhonCharacter::StopRun()
{
	ApplyRunning(false);
	ServerSetRunning(false);
}

void AErewhonCharacter::ApplyRunning(bool bRunning)
{
	bIsRunning = bRunning;
	UpdateGroundSpeed();
}

void AErewhonCharacter::UpdateGroundSpeed()
{
	// Le terrain freine : hautes herbes, buissons. Calculé à l'identique par le serveur et
	// par le client propriétaire, à partir des mêmes données déterministes du secteur.
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	float Factor = 1.f;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	USectorLayoutSubsystem* Layouts = GetWorld()->GetSubsystem<USectorLayoutSubsystem>();
	if (State && Layouts)
	{
		const FVector Location = GetActorLocation();
		Factor = Layouts->GetLayout(ErewhonSector::FromWorld(Location), State->GetWorldSeed())->SpeedFactor(FVector2D(Location));
		if (HasPotion(EPotionEffect::Calm))
		{
			Factor = FMath::Max(Factor, 1.f);   // sérénité : herbes hautes et buissons ne retiennent plus
		}
	}
	// Un outil attelable tiré à pied : fait pour un bhurak, il se traîne levé (20 %), et ne bouge pas baissé.
	if (DrivenCart && DrivenCart->IsImplement())
	{
		Factor *= DrivenCart->IsEngaged() ? 0.f : 0.2f;
	}
	else if (!HasPotion(EPotionEffect::Strength))
	{
		// On pousse une charrette, on porte un tonneau ou un meuble : moitié moins vite ; une cagette : un peu moins (sauf potion de force).
		if (DrivenCart || (Carried && ErewhonPlaceables::IsHeavy(Carried->GetKind())))
		{
			Factor *= 0.5f;
		}
		else if (Carried && Carried->GetKind() == EStorageKind::Crate)
		{
			Factor *= 0.8f;
		}
	}
	// Trop chargé, on ralentit, jusqu'à ne plus pouvoir avancer.
	{
		const float Load = GetCarriedWeight();
		const float Comfort = GetCarryComfort();
		const float Max = GetCarryMax();
		if (Load >= Max)
		{
			Factor = 0.f;
		}
		else if (Load > Comfort)
		{
			Factor *= FMath::Lerp(1.f, 0.25f, (Load - Comfort) / (Max - Comfort));
		}
	}
	if (Mount)
	{
		Factor *= ABhurak::RideSpeedFactor;
		if (const ACart* Towed = Mount->GetTowedCart())
		{
			Factor *= Towed->DragFactor();   // cultivateur baissé, semoir enclenché
		}
	}
	if (HasPotion(EPotionEffect::Speed))
	{
		Factor *= 1.6f;
	}
	// Attelé (charrette ou outil derrière le bhurak) : on ne galope pas.
	const bool bTowing = Mount && Mount->GetTowedCart();
	const float Base = bIsRunning && !bTowing ? Settings.RunSpeedMetersPerSecond : Settings.WalkSpeedMetersPerSecond;
	GetCharacterMovement()->MaxWalkSpeed = Base * Factor * 100.f;
	GetCharacterMovement()->MaxWalkSpeedCrouched = Settings.WalkSpeedMetersPerSecond * 0.5f * Factor * 100.f;
}

void AErewhonCharacter::ServerSetRunning_Implementation(bool bRunning)
{
	ApplyRunning(bRunning);
}

void AErewhonCharacter::SelectHotbar(int32 Hotbar)
{
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	if (State && !IsMenuOpen() && Hotbar != State->GetInventory()->GetActiveHotbar())
	{
		State->GetInventory()->ServerSelectHotbar(Hotbar);
	}
}

void AErewhonCharacter::ScrollHotbar(const FInputActionValue& Value)
{
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	const float Axis = Value.Get<float>();
	if (!State || IsMenuOpen() || FMath::IsNearlyZero(Axis))
	{
		return;
	}
	// Molette vers le bas : l'emplacement suivant ; vers le haut : le précédent.
	const int32 Size = FMath::Max(1, State->GetInventory()->GetHotbarSize());
	const int32 Next = (State->GetInventory()->GetActiveHotbar() + (Axis < 0.f ? 1 : -1) + Size) % Size;
	State->GetInventory()->ServerSelectHotbar(Next);
}

void AErewhonCharacter::ApplyStowed()
{
	const UInventoryComponent* Inventory = BoundInventory.Get();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!Inventory || !Items || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const TArray<FName>& Stowed = Inventory->GetAppearance().Stowed;
	while (StowedParts.Num() < Stowed.Num())
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetupAttachment(GetCapsuleComponent());
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetOwnerNoSee(ViewMode == EViewMode::FirstPerson);
		Part->RegisterComponent();
		AddInstanceComponent(Part);
		StowedParts.Add(Part);
	}
	const bool bBag = BoundBackpack.IsValid() && BoundBackpack->IsWorn();
	int32 Tools = 0, Belt = 0;
	for (int32 i = 0; i < StowedParts.Num(); ++i)
	{
		UStaticMeshComponent* Part = StowedParts[i];
		const FItemDef* Def = Stowed.IsValidIndex(i) ? Items->Find(Stowed[i]) : nullptr;
		UStaticMesh* StowMesh = Def ? Cast<UStaticMesh>(Def->Mesh.TryLoad()) : nullptr;
		Part->SetVisibility(StowMesh != nullptr && !Def->IsPlaceable());
		if (!StowMesh || Def->IsPlaceable())
		{
			continue;
		}
		Part->SetStaticMesh(StowMesh);
		FTransform Place;
		if (Def->Category == EItemCategory::Tool)
		{
			// En croix dans le dos, comme rangés à un harnais : à plat contre le dos (derrière le sac s'il est porté),
			// l'un penché à droite, l'autre à gauche, qui se croisent entre les omoplates.
			const float Side = Tools == 0 ? 1.f : -1.f;
			const float Behind = (bBag ? -46.f : -22.f) - 3.f * Tools;
			++Tools;
			const float Height = Def->Id == ErewhonFarm::Scythe ? 100.f : Def->Id == ErewhonFarm::Spade ? 85.f : 55.f;
			// Le long de sa plus grande dimension (un modèle couché, comme la faux, se relève d'abord), centré.
			const FVector Size = StowMesh->GetBoundingBox().GetSize();
			const FTransform Upright = Size.Z >= FMath::Max(Size.X, Size.Y)
				? FitToHeight(StowMesh, Height) * FTransform(FVector(0.f, 0.f, -Height * 0.5f)) * FTransform(FRotator(0.f, 90.f, 0.f))
				: FitToLength(StowMesh, Height) * FTransform(FRotator(0.f, 0.f, 90.f));   // la longueur (sur Y) passe à la verticale
			// Le manche de la faux est déjà en biais dans son modèle (~40°) : on n'ajoute que ce qui manque.
			const float Lean = Def->Id == ErewhonFarm::Scythe ? -40.f : 0.f;
			Place = Upright * FTransform(FRotator(0.f, 0.f, Side * 42.f - Lean), FVector(Behind, 0.f, 28.f));   // penché, dans le plan du dos
		}
		else
		{
			// À la ceinture, sur la hanche.
			const float Side = Belt++ == 0 ? 1.f : -1.f;
			Place = FitToHeight(StowMesh, 18.f) * FTransform(FRotator::ZeroRotator, FVector(4.f, Side * 30.f, -10.f));
		}
		Part->SetRelativeTransform(Place);
	}
	// Les sangles du harnais, dès qu'un outil est rangé dans le dos : deux fines lanières croisées, contre le dos.
	if (HarnessStraps.IsEmpty())
	{
		const FLinearColor Leather = FLinearColor::FromSRGBColor(FColor(84, 56, 36));
		for (const float Side : { 1.f, -1.f })
		{
			UStaticMeshComponent* Strap = AddShape(this, GetCapsuleComponent(), EShape::Cube, FVector(-19.f, 0.f, 30.f),
				FVector(1.5f, 3.f, 46.f), Leather, FRotator(0.f, 0.f, Side * 40.f), false);
			Strap->SetOwnerNoSee(ViewMode == EViewMode::FirstPerson);
			HarnessStraps.Add(Strap);
		}
	}
	for (UStaticMeshComponent* Strap : HarnessStraps)
	{
		Strap->SetVisibility(Tools > 0);
	}
}

void AErewhonCharacter::ToggleInventory()
{
	if (AErewhonPlayerController* PC = GetController<AErewhonPlayerController>())
	{
		PC->ToggleInventory();
	}
}

void AErewhonCharacter::BagAction()
{
	// F : en visant son sac posé, on le remet sur le dos ; sinon, on pose celui qu'on porte.
	AErewhonPlayerController* PC = GetController<AErewhonPlayerController>();
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	if (!PC || !State || PC->IsMenuOpen())
	{
		return;
	}
	if (DrivenCart)
	{
		PC->ServerToggleDrive(nullptr);   // lâcher la charrette
		return;
	}
	if (Mount)
	{
		PC->ServerRideBhurak(nullptr);   // descendre
		return;
	}
	if (ABhurak* Beast = Cast<ABhurak>(Target.Interactable.Get()))
	{
		if (!Beast->IsOwnedBy(State->GetPlayerKey()))
		{
			PC->ShowToast(LOCTEXT("NotYourBhurak", "Ce bhurak n'est pas à vous."));
		}
		else if (Beast->HasSaddle())
		{
			PC->ServerRideBhurak(Beast);
		}
		else
		{
			PC->ShowToast(LOCTEXT("NoSaddle", "Il faut une selle pour le monter : [E] pour le harnacher."));
		}
		return;
	}
	if (AStorageActor* Storage = Cast<AStorageActor>(Target.Interactable.Get()))
	{
		// Un pichet en main : F y puise au récipient visé, un litre par seconde (F de nouveau : on s'arrête).
		if (TryPitcher(Storage, true))
		{
			return;
		}
		// Le rack à petit bois : chacun peut s'y servir.
		if (Storage->GetKind() == EStorageKind::FirewoodRack)
		{
			PC->OpenStorage(Storage);
			return;
		}
		if (Storage->IsOwnedBy(State->GetPlayerKey()))
		{
			// Les outils de l'apothicaire : à l'apothicaire seulement.
			const EStorageKind StorageKind = Storage->GetKind();
			const bool bTool = StorageKind == EStorageKind::Mortar || StorageKind == EStorageKind::Cauldron || StorageKind == EStorageKind::Workbench
				|| StorageKind == EStorageKind::DryingRack || StorageKind == EStorageKind::CauldronStand;
			if (bTool && !State->IsApothecary())
			{
				PC->ShowToast(LOCTEXT("ApothecaryOnly", "Réservé aux apothicaires : choisissez cette classe à la mairie."));
				return;
			}
			// Le mortier et le chaudron servent à préparer ; la table ne s'ouvre pas.
			switch (StorageKind)
			{
			case EStorageKind::Cauldron:
				PC->OpenCauldron(Storage);
				break;
			case EStorageKind::CauldronStand:
			{
				// Le support : on règle le chaudron qui y pend.
				AStorageActor* Hung = nullptr;
				for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It)
				{
					if (It->GetKind() == EStorageKind::Cauldron && FVector::Dist(It->GetActorLocation(), ErewhonApothecary::CauldronSpotOn(Storage).GetLocation()) <= 20.f)
					{
						Hung = *It;
					}
				}
				if (Hung)
				{
					PC->OpenCauldron(Hung);
				}
				else
				{
					PC->ShowToast(LOCTEXT("StandEmpty", "Posez un chaudron sur le support : prenez-le et visez la cheminée."));
				}
				break;
			}
			case EStorageKind::Mortar:
			case EStorageKind::Workbench:
				PC->OpenApothecary(Storage, static_cast<uint8>(StorageKind == EStorageKind::Workbench ? ErewhonApothecary::ECraft::Workbench : ErewhonApothecary::ECraft::Mortar));
				break;
			case EStorageKind::Potpourri:
				break;   // il parfume, c'est tout
			case EStorageKind::LogPile:
				PC->ServerTakeLogs(Storage);   // un tas ne s'ouvre pas : on en reprend
				break;
			case EStorageKind::Table:
				PC->ShowToast(LOCTEXT("TableHint", "Posez-y un mortier : prenez-le en main et visez la table."));
				break;
			default:
				PC->OpenStorage(Storage);
				break;
			}
		}
		else
		{
			PC->ShowToast(LOCTEXT("NotYours", "Ce n'est pas à vous."));
		}
		return;
	}
	if (ACart* Cart = Cast<ACart>(Target.Interactable.Get()))
	{
		// Devant les brancards : on la conduit ; ailleurs, on ouvre son chargement de petit bois.
		if (Cart->IsAtHandles(GetActorLocation()) || !Cart->IsOwnedBy(State->GetPlayerKey()))
		{
			PC->ServerToggleDrive(Cart);
		}
		else if (Cart->GetCartKind() == ECartKind::Cultivator)
		{
			PC->ServerToggleImplement(Cart);
		}
		else
		{
			PC->OpenCart(Cart);
		}
		return;
	}
	if (ABackpack* Aimed = Cast<ABackpack>(Target.Interactable.Get()))
	{
		if (Aimed->IsOwnedBy(State->GetPlayerKey()))
		{
			PC->ServerPickUpBackpack(Aimed);
		}
		else
		{
			PC->ShowToast(LOCTEXT("NotYourBag", "Ce sac n'est pas à vous."));
		}
		return;
	}
	const UBackpackComponent* Bag = State->GetBackpack();
	if (Bag->IsWorn())
	{
		PC->ServerDropBackpack();
	}
	else if (Bag->IsOwned())
	{
		PC->ShowToast(LOCTEXT("BagElsewhere", "Votre sac est posé ailleurs : allez le chercher."));
	}
}

void AErewhonCharacter::StashAction()
{
	// G : en visant son sac posé, on y vide ses poches.
	AErewhonPlayerController* PC = GetController<AErewhonPlayerController>();
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	// Un pichet en main : G verse dans le récipient visé, un litre par seconde (G de nouveau : on s'arrête).
	if (PC && State && !PC->IsMenuOpen())
	{
		if (AStorageActor* Aimed = Cast<AStorageActor>(Target.Interactable.Get()); Aimed && TryPitcher(Aimed, false))
		{
			return;
		}
	}
	// Le bhurak : en selle ou en conduisant une charrette, on attelle ; sur un bhurak attelé, on dételle.
	if (PC && State && !PC->IsMenuOpen())
	{
		ABhurak* AimedBeast = Cast<ABhurak>(Target.Interactable.Get());
		if (Mount || DrivenCart || (AimedBeast && AimedBeast->GetTowedCart()))
		{
			PC->ServerHitchBhurak(AimedBeast);
			return;
		}
	}
	const ABackpack* Aimed = Cast<ABackpack>(Target.Interactable.Get());
	if (PC && State && !PC->IsMenuOpen() && Aimed && Aimed->IsOwnedBy(State->GetPlayerKey()))
	{
		PC->ServerStashAll();
	}
	AStorageActor* Storage = Cast<AStorageActor>(Target.Interactable.Get());
	if (PC && State && !PC->IsMenuOpen() && Storage && Storage->IsOwnedBy(State->GetPlayerKey()))
	{
		PC->ServerStashInStorage(Storage);
	}
}

void AErewhonCharacter::StartCrouch()
{
	Crouch();
}

void AErewhonCharacter::StopCrouch()
{
	UnCrouch();
}

void AErewhonCharacter::Use()
{
	AErewhonPlayerController* PC = GetController<AErewhonPlayerController>();
	if (!PC || PC->IsMenuOpen())
	{
		return;
	}
	if (IsSeated())
	{
		ServerStand();
		return;
	}
	if (IErewhonInteractable* Interactable = Cast<IErewhonInteractable>(Target.Interactable.Get()))
	{
		Interactable->Interact(PC);
	}
}

void AErewhonCharacter::Primary()
{
	AErewhonPlayerController* PC = GetController<AErewhonPlayerController>();
	if (!PC || PC->IsMenuOpen())
	{
		return;
	}

	if (IsPlacing())
	{
		if (Placement.bValid)
		{
			PC->ServerPlace(Placement.Type, Placement.Location, Placement.Yaw, Placement.Target.Get(), Placement.Slot);
		}
		else if (Placement.bForbidden)
		{
			PC->ShowToast(LOCTEXT("NotHere", "Seulement sur vos secteurs (ou, pour une cagette, dans une charrette)."));
		}
		else
		{
			PC->ShowToast(LOCTEXT("NoPlace", "Visez le sol (ou une cagette, une charrette) pour le poser."));
		}
		return;
	}
	// Le pichet en main, devant un tonneau ou le chaudron : le clic gauche puise.
	if (AStorageActor* Vessel = Cast<AStorageActor>(Target.Interactable.Get()); Vessel && TryPitcher(Vessel, true))
	{
		return;
	}
	if (IErewhonInteractable* Interactable = Cast<IErewhonInteractable>(Target.Interactable.Get()))
	{
		Interactable->Interact(PC);
		return;
	}
	// En selle, un outil attelé : le clic actionne son levier.
	if (Mount)
	{
		if (ACart* Towed = Mount->GetTowedCart(); Towed && Towed->IsImplement())
		{
			PC->ServerToggleImplement(Towed);
		}
		return;
	}

	// Un outil en main : chaque clic donne son coup (et son bruit), repris depuis le début s'il était en cours.
	const AErewhonPlayerState* MyState = GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* HandDef = MyState && Items ? Items->Find(MyState->GetInventory()->GetHandItem()) : nullptr;
	const FName HandId = HandDef ? HandDef->Id : NAME_None;
	// La faux : un grand geste de deux secondes, et tout ce qui est à portée tombe.
	if (HandId == ErewhonFarm::Scythe)
	{
		if (ScytheTime <= 0.f)
		{
			ScytheTime = ErewhonFarm::ScytheSeconds;
			PlayToolSound(TEXT("scythe_sweep"), 0.8f);
			PC->ServerScythe();
		}
		return;
	}
	// La hache : un coup dans le tronc visé, au rythme du geste.
	if (HandId == ErewhonForestry::Axe)
	{
		if (Target.TreeIndex != INDEX_NONE && SwingTime <= 0.f)
		{
			SwingTime = GSwingDuration;
			PlayToolSound(TEXT("axe_chop"), 0.8f);
			PC->ServerChopTree(Target.TreeSector, Target.TreeIndex);
		}
		return;
	}
	// La bêche sur son champ : un coup d'une seconde, pas deux à la fois.
	if (HandId == ErewhonFarm::Spade && Target.FieldCell != INDEX_NONE)
	{
		if (DigTime <= 0.f)
		{
			DigTime = ErewhonFarm::SpadeSeconds;
			PlayToolSound(TEXT("spade_dig"), 0.7f);
			PC->ServerWorkField(Target.FieldSector, Target.FieldCell, Target.FieldCorner);
		}
		return;
	}
	// Une cueillette (ou un autre geste) en cours : on attend qu'elle finisse.
	if (PC->GetCraftJob().IsActive())
	{
		return;
	}
	const bool bSeeds = ErewhonFarm::CropOfSeed(HandId) != INDEX_NONE;
	if (HandDef && (HandDef->Category == EItemCategory::Tool || (bSeeds && Target.FieldCell != INDEX_NONE)))
	{
		SwingTime = GSwingDuration;
		PlayToolSound(HandId == ErewhonFarm::Spade ? TEXT("spade_dig") : bSeeds ? TEXT("seeds_scatter") : TEXT("sickle_swish"), 0.7f);
	}
	// Une case de son champ : bêcher, semer, récolter.
	if (Target.FieldCell != INDEX_NONE)
	{
		PC->ServerWorkField(Target.FieldSector, Target.FieldCell, Target.FieldCorner);
		if (HandId.IsNone() && AnimatedBody && GetVelocity().Size2D() < 20.f && BodyAnimations.Contains(TEXT("pickup")))
		{
			PlayBodyAnimation(TEXT("pickup"), false, 1.8f);
			OneShotUntil = GetWorld()->GetTimeSeconds() + BodyAnimations[TEXT("pickup")]->GetPlayLength() / 1.8f;
		}
		return;
	}
	if (!Target.Plant.IsValid())
	{
		return;
	}
	if (!Target.bPlantHarvestable)
	{
		PC->ShowToast(LOCTEXT("BuySectorFirst", "Achetez ce secteur au marchand pour y récolter."));
		return;
	}
	PC->ServerHarvest(Target.Plant.Sector, Target.Plant.PlantIndex);
	PendingHarvests.Add({ Target.Plant.Sector, Target.Plant.PlantIndex }, GetWorld()->GetTimeSeconds());
	if (AnimatedBody && GetVelocity().Size2D() < 20.f && BodyAnimations.Contains(TEXT("pickup")))
	{
		PlayBodyAnimation(TEXT("pickup"), false, 1.8f);
		OneShotUntil = GetWorld()->GetTimeSeconds() + BodyAnimations[TEXT("pickup")]->GetPlayLength() / 1.8f;
	}
	SwingTime = GSwingDuration;
}

// ── Ciblage ──────────────────────────────────────────────────────────────────

void AErewhonCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() || IsLocallyControlled())
	{
		UpdateGroundSpeed();
	}
	UpdatePotions(DeltaSeconds);
	if (GetNetMode() != NM_DedicatedServer)
	{
		AttachBackpackToSpine();
		UpdateAnimation(DeltaSeconds);
		UpdateFootsteps(DeltaSeconds);
	}
	if (!IsLocallyControlled())
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	// -ErewhonTestWalk=8 : tests, on avance tout droit pendant 8 s (après 6 s de chargement).
	if (float WalkFor = 0.f; FParse::Value(FCommandLine::Get(), TEXT("ErewhonTestWalk="), WalkFor))
	{
		const float Age = GetGameTimeSinceCreation();
		if (Age > 6.f && Age < 6.f + WalkFor)
		{
			AddMovementInput(FRotator(0.f, GetControlRotation().Yaw, 0.f).Vector(), 1.f);
		}
	}
#endif
	UpdateTarget();
	UpdateHighlight();
	UpdatePlacement();

#if !UE_BUILD_SHIPPING
	// -ErewhonTestScythe=0.6 : tests, le geste de la faux figé à cet instant (0 à 1).
	if (float Frozen = -1.f; FParse::Value(FCommandLine::Get(), TEXT("ErewhonTestScythe="), Frozen))
	{
		ScytheTime = FMath::Max(0.01f, (1.f - Frozen) * ErewhonFarm::ScytheSeconds) + DeltaSeconds;
	}
#endif
	// La faux : on arme vers la droite, on fauche largement de droite à gauche, lame au ras du sol, on revient.
	// Le geste pivote autour des mains, dans le repère de la vue.
	if (ScytheTime > 0.f)
	{
		ScytheTime = FMath::Max(0.f, ScytheTime - DeltaSeconds);
		const float P = 1.f - ScytheTime / ErewhonFarm::ScytheSeconds;
		const float Yaw = P < 0.35f ? FMath::InterpEaseInOut(0.f, 55.f, P / 0.35f, 2.f)
			: P < 0.8f ? FMath::InterpEaseInOut(55.f, -75.f, (P - 0.35f) / 0.45f, 2.f)
			: FMath::InterpEaseInOut(-75.f, 0.f, (P - 0.8f) / 0.2f, 2.f);
		// En armant, la lame se soulève un peu ; en fauchant, elle rase le sol.
		const float Lift = P < 0.35f ? FMath::Sin(P / 0.35f * UE_HALF_PI) * 12.f : P < 0.8f ? 12.f * (1.f - (P - 0.35f) / 0.45f) : 0.f;
		const FQuat Sweep = FQuat(FRotator(0.f, Yaw, 0.f)) * FQuat(FRotator(Lift, 0.f, 0.f)) * FQuat(HeldFirstRotation);
		HeldFirstPerson->SetRelativeLocationAndRotation(HeldFirstHand + Sweep.RotateVector(HeldFirstPivotOffset), Sweep);
		if (ScytheTime <= 0.f)
		{
			HeldFirstPerson->SetRelativeLocationAndRotation(HeldFirstLocation, HeldFirstRotation);
		}
	}
	// La bêche : le bas du fer part vers l'avant et s'enfonce, on fait levier (le haut du manche revient), puis on se redresse.
	else if (DigTime > 0.f)
	{
		DigTime = FMath::Max(0.f, DigTime - DeltaSeconds);
		const float P = 1.f - DigTime / ErewhonFarm::SpadeSeconds;
		const float Thrust = P < 0.4f ? FMath::InterpEaseInOut(0.f, 1.f, P / 0.4f, 2.f)
			: P < 0.75f ? 1.f : FMath::InterpEaseInOut(1.f, 0.f, (P - 0.75f) / 0.25f, 2.f);
		const float Lever = P < 0.4f ? 0.f : P < 0.75f ? FMath::InterpEaseInOut(0.f, 1.f, (P - 0.4f) / 0.35f, 2.f)
			: FMath::InterpEaseInOut(1.f, 0.f, (P - 0.75f) / 0.25f, 2.f);
		HeldFirstPerson->SetRelativeLocation(HeldFirstLocation + FVector(30.f * Thrust, -6.f * Thrust, -14.f * Thrust));
		// Basculé dans le repère de la vue (pas dans celui de la bêche, tournée) : le haut du manche part en arrière.
		HeldFirstPerson->SetRelativeRotation((FQuat(FRotator(15.f * Thrust + 25.f * Lever, 0.f, 0.f)) * FQuat(HeldFirstRotation)).Rotator());
		if (DigTime <= 0.f)
		{
			HeldFirstPerson->SetRelativeLocation(HeldFirstLocation);
			HeldFirstPerson->SetRelativeRotation(HeldFirstRotation);
		}
	}
	// Coup de l'objet tenu : un aller-retour vers le bas.
	else if (SwingTime > 0.f)
	{
		SwingTime = FMath::Max(0.f, SwingTime - DeltaSeconds);
		const float Progress = 1.f - SwingTime / GSwingDuration;
		const float Arc = FMath::Sin(Progress * UE_PI) * 55.f;
		HeldFirstPerson->SetRelativeRotation(HeldFirstRotation + FRotator(-Arc, -Arc * 0.4f, 0.f));
	}
}

float AErewhonCharacter::PotionRemaining(EPotionEffect Effect) const
{
	const int32 Index = static_cast<int32>(Effect);
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State || !PotionUntil.IsValidIndex(Index))
	{
		return 0.f;
	}
	return FMath::Max(0.f, static_cast<float>(PotionUntil[Index] - State->GetWorldClock()));
}

void AErewhonCharacter::DrinkPotion(EPotionEffect Effect, float Seconds)
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State || Effect == EPotionEffect::None)
	{
		return;
	}
	PotionUntil.SetNum(static_cast<int32>(EPotionEffect::Count));   // (un effet ajouté au jeu agrandit le tableau)
	// Rapetisser et grandir s'annulent : la dernière potion bue l'emporte.
	if (Effect == EPotionEffect::Shrink || Effect == EPotionEffect::Grow)
	{
		PotionUntil[static_cast<int32>(EPotionEffect::Shrink)] = 0.0;
		PotionUntil[static_cast<int32>(EPotionEffect::Grow)] = 0.0;
	}
	PotionUntil[static_cast<int32>(Effect)] = State->GetWorldClock() + (Seconds > 0.f ? Seconds : ErewhonApothecary::PotionSeconds);
	MulticastPotionDrunk();
}

void AErewhonCharacter::GrantEffect(EPotionEffect Effect, double Seconds)
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State || Effect == EPotionEffect::None)
	{
		return;
	}
	PotionUntil.SetNum(static_cast<int32>(EPotionEffect::Count));
	PotionUntil[static_cast<int32>(Effect)] = FMath::Max(PotionUntil[static_cast<int32>(Effect)], State->GetWorldClock() + Seconds);
}

void AErewhonCharacter::MulticastPotionDrunk_Implementation()
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		ErewhonSound::PlayAt(this, ErewhonSound::Load(TEXT("potion_drink")), GetActorLocation(), 0.8f, 150.f, 1200.f);
	}
}

void AErewhonCharacter::UpdatePotions(float DeltaSeconds)
{
	// Taille : on rapetisse ou on grandit en une seconde environ, et on revient de même.
	const float TargetScale = HasPotion(EPotionEffect::Shrink) ? 0.45f : HasPotion(EPotionEffect::Grow) ? 1.8f : 1.f;
	if (!FMath::IsNearlyEqual(BodyScale, TargetScale, 0.001f))
	{
		BodyScale = FMath::FInterpTo(BodyScale, TargetScale, DeltaSeconds, 3.f);
		if (FMath::IsNearlyEqual(BodyScale, TargetScale, 0.002f))
		{
			BodyScale = TargetScale;
		}
		SetActorScale3D(FVector(BodyScale));
	}
	if (HasAuthority() || IsLocallyControlled())
	{
		// Saut : la hauteur croît comme le carré de l'élan (×1,55 → un peu plus de 2 m).
		GetCharacterMovement()->JumpZVelocity = 450.f * (HasPotion(EPotionEffect::Jump) ? 1.55f : 1.f);
	}

	// Lumière : une lueur chaude autour de soi, que tout le monde voit.
	if (GetNetMode() != NM_DedicatedServer)
	{
		const float WantedGlow = HasPotion(EPotionEffect::Light) ? 1.f : 0.f;
		if (WantedGlow > 0.f || GlowLevel > 0.f)
		{
			GlowLevel = FMath::FInterpConstantTo(GlowLevel, WantedGlow, DeltaSeconds, 0.8f);
			if (!PotionGlow)
			{
				PotionGlow = NewObject<UPointLightComponent>(this);
				PotionGlow->SetupAttachment(GetCapsuleComponent());
				PotionGlow->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
				PotionGlow->SetIntensityUnits(ELightUnits::Lumens);
				PotionGlow->SetAttenuationRadius(1400.f);
				PotionGlow->SetLightColor(FLinearColor(1.f, 0.82f, 0.55f));
				PotionGlow->SetSourceRadius(30.f);
				PotionGlow->SetCastShadows(false);
				PotionGlow->RegisterComponent();
			}
			const float Flicker = 1.f + 0.05f * FMath::Sin(GetWorld()->GetTimeSeconds() * 3.1f);
			PotionGlow->SetIntensity(6000.f * GlowLevel * Flicker);
			PotionGlow->SetVisibility(GlowLevel > 0.001f);
		}
	}

	// Nyctalopie (pour soi seulement) : l'œil s'ouvre grand dans le noir, les couleurs pâlissent un peu.
	if (!IsLocallyControlled())
	{
		return;
	}
	const float Wanted = HasPotion(EPotionEffect::Night) ? 1.f : 0.f;
	if (FMath::IsNearlyEqual(NightVision, Wanted) && Wanted == 0.f)
	{
		return;
	}
	NightVision = FMath::FInterpConstantTo(NightVision, Wanted, DeltaSeconds, 0.7f);
	for (UCameraComponent* View : { Camera.Get(), ThirdCamera.Get() })
	{
		FPostProcessSettings& PP = View->PostProcessSettings;
		PP.bOverride_AutoExposureMinBrightness = NightVision > 0.f;
		PP.AutoExposureMinBrightness = FMath::Lerp(-1.5f, -7.f, NightVision);
		PP.bOverride_AutoExposureBias = NightVision > 0.f;
		PP.AutoExposureBias = FMath::Lerp(-0.5f, 1.2f, NightVision);
		PP.bOverride_ColorSaturation = NightVision > 0.f;
		PP.ColorSaturation = FVector4(1.f, 1.f, 1.f, FMath::Lerp(1.12f, 0.8f, NightVision));
		PP.bOverride_ColorGain = NightVision > 0.f;
		PP.ColorGain = FVector4(FMath::Lerp(1.f, 0.92f, NightVision), FMath::Lerp(1.f, 1.04f, NightVision), FMath::Lerp(1.f, 1.08f, NightVision), 1.f);
		View->PostProcessBlendWeight = 1.f;
	}
}

void AErewhonCharacter::UpdateFootsteps(float DeltaSeconds)
{
	// Un son par pas, à une cadence réaliste : ~1,8 pas/s en flânant, ~2,2 en marchant, ~2,8 en courant.
	// La foulée s'en déduit, si bien que ralentir dans l'herbe espace aussi les pas. Son selon la surface.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();
	if (!Movement->IsMovingOnGround() || Speed < 40.f || Mount)
	{
		StrideDistance = 0.f;
		return;
	}
	StrideDistance += Speed * DeltaSeconds;
	const float Cadence = FMath::Clamp(1.4f + Speed / 500.f, 1.5f, 2.9f);   // pas par seconde
	const float Stride = Speed / Cadence;
	if (StrideDistance < Stride)
	{
		return;
	}
	StrideDistance -= Stride;

	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FString Prefix = TEXT("step_grass");
	int32 Count = 3;
	if (ErewhonSpawn::IsInsideCabin(Feet))
	{
		Prefix = TEXT("step_stone");
		Count = 2;
	}
	else if (ErewhonApothecary::IsInside(this, Feet))
	{
		Prefix = TEXT("step_wood");   // le plancher de l'apothicairerie
		Count = 2;
	}
	else if (ErewhonSpawn::IsOnPorch(Feet))
	{
		Prefix = TEXT("step_wood");
		Count = 2;
	}
	else if (const AErewhonGameState* State = AErewhonGameState::Get(this))
	{
		const TSharedRef<const FSectorLayout> Layout = GetWorld()->GetSubsystem<USectorLayoutSubsystem>()->GetLayout(ErewhonSector::FromWorld(Feet), State->GetWorldSeed());
		if (Layout->PavedAmount(FVector2D(Feet)) > 0.4f)
		{
			Prefix = TEXT("step_stone");
			Count = 2;
		}
		else if (Layout->DirtAmount(FVector2D(Feet)) > 0.4f)
		{
			Prefix = TEXT("step_dirt");
		}
	}
	const float Volume = (bIsCrouched ? 0.25f : Speed > 500.f ? 0.7f : 0.5f) * (IsLocallyControlled() ? 0.8f : 1.f);
	ErewhonSound::PlayAt(this, ErewhonSound::LoadVariant(Prefix, Count), Feet, Volume, 200.f, 2200.f);
}

bool AErewhonCharacter::IsPlacing() const
{
	if (Carried)
	{
		return true;
	}
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* Hand = State && Items ? Items->Find(State->GetInventory()->GetHandItem()) : nullptr;
	return Hand && (Hand->IsPlaceable() || Hand->Id == ErewhonForestry::Log);   // des bûches en main : on les pose en tas
}

void AErewhonCharacter::UpdatePlacement()
{
	Placement = FPlacement();
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	if (!IsLocallyControlled() || !State || !IsPlacing() || IsMenuOpen())
	{
		if (PlacementGhost)
		{
			PlacementGhost->SetVisibility(false);
		}
		return;
	}

	// Ce qu'on pose : la charrette, ou un conteneur (porté, ou neuf depuis la main).
	const FName HandItem = State->GetInventory()->GetHandItem();
	const bool bBhurak = !Carried && ErewhonPlaceables::IsBhurak(HandItem);
	const bool bCart = !Carried && (ErewhonPlaceables::IsCart(HandItem) || bBhurak);
	const TOptional<EStorageKind> HandKind = ErewhonPlaceables::StorageForItem(HandItem);
	const EStorageKind Kind = Carried ? Carried->GetKind() : HandKind.Get(EStorageKind::Crate);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonPlacement), false, this);
	if (Carried)
	{
		Params.AddIgnoredActor(Carried);
	}
	const FVector Eye = Camera->GetComponentLocation();
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + GetControlRotation().Vector() * 600.f, ECC_Visibility, Params))
	{
		if (PlacementGhost)
		{
			PlacementGhost->SetVisibility(false);
		}
		return;
	}

	FTransform At;
	AActor* HitActor = Hit.GetActor();
	AStorageActor* HitStorage = Cast<AStorageActor>(HitActor);
	ACart* HitCart = Cast<ACart>(HitActor);
	if (HitStorage && HitStorage->GetCart())
	{
		HitCart = HitStorage->GetCart();   // viser un conteneur dans la charrette, c'est viser la charrette
	}
	const FString& Key = State->GetPlayerKey();
	const bool bFurniture = !bCart && ErewhonPlaceables::IsFurniture(Kind);
	if (!bCart && Kind == EStorageKind::Mortar && HitStorage && HitStorage->IsOwnedBy(Key) && HitStorage->GetKind() == EStorageKind::Table
		&& !HitStorage->GetAbove() && !HitStorage->GetCarrier())
	{
		// Le mortier sur la table.
		Placement.Type = 1;
		Placement.Target = HitStorage;
		At = FTransform(HitStorage->GetActorRotation(), HitStorage->GetActorLocation() + FVector(0, 0, ErewhonPlaceables::Height(EStorageKind::Table)));
	}
	else if (!bCart && Kind == EStorageKind::LogPile && HitStorage && HitStorage->IsOwnedBy(Key) && HitStorage->GetKind() == EStorageKind::LogPile)
	{
		// Sur son tas de bûches.
		Placement.Type = 1;
		Placement.Target = HitStorage;
		At = HitStorage->GetActorTransform();
	}
	else if (!bCart && Kind == EStorageKind::Crate && HitStorage && HitStorage->IsOwnedBy(Key) && HitStorage->GetKind() == EStorageKind::Crate
		&& HitStorage->TopOfStack()->StackSize() < ErewhonPlaceables::MaxStack && !HitStorage->TopOfStack()->GetCarrier())
	{
		// Sur le haut de la pile.
		AStorageActor* Top = HitStorage->TopOfStack();
		Placement.Type = 1;
		Placement.Target = Top;
		At = FTransform(Top->GetActorRotation(), Top->GetActorLocation() + FVector(0, 0, ErewhonPlaceables::Height(Top->GetKind())));
	}
	else if (!bCart && !bFurniture && HitCart && HitCart->IsOwnedBy(Key) && HitCart->FreeSlotNear(Hit.ImpactPoint) != INDEX_NONE)
	{
		// Dans la charrette : l'emplacement libre le plus proche s'éclaire.
		Placement.Type = 2;
		Placement.Target = HitCart;
		Placement.Slot = HitCart->FreeSlotNear(Hit.ImpactPoint);
		At = ACart::SlotTransform(Placement.Slot) * HitCart->GetActorTransform();
	}
	else if (Hit.ImpactNormal.Z > 0.6f && !HitStorage && !HitCart)
	{
		// Au sol, face au joueur (les brancards de la charrette vers lui, la façade d'un meuble aussi).
		Placement.Type = 0;
		At = FTransform(FRotator(0.f, GetActorRotation().Yaw, 0.f), Hit.ImpactPoint);   // la façade d'un meuble (−X) vers soi
		if (const TOptional<FTransform> Hook = bCart ? TOptional<FTransform>() : ErewhonPlaceables::HookFor(this, Kind, Hit.ImpactPoint))
		{
			At = *Hook;   // le chaudron s'accroche à la crémaillère
		}
		else if (const TOptional<FVector> Under = bCart ? TOptional<FVector>() : ErewhonPlaceables::FountainSnap(this, Kind, Hit.ImpactPoint))
		{
			At = FTransform(At.Rotator(), *Under);   // le tonneau se cale sous le bec de la fontaine
		}
	}
	else
	{
		if (PlacementGhost)
		{
			PlacementGhost->SetVisibility(false);
		}
		return;
	}
	Placement.Location = At.GetLocation();
	Placement.Yaw = At.Rotator().Yaw;
	// Hors de ses secteurs, on ne pose que la charrette, ou une cagette dans une charrette : l'aperçu rougit.
	Placement.bForbidden = !ErewhonPlaceables::CanPlaceAt(this, Key, bCart, Kind, Placement.Type, Placement.Location);
	Placement.bValid = !Placement.bForbidden;

	// L'aperçu : le modèle de ce qu'on pose, en halo translucide.
	if (!PlacementGhost)
	{
		PlacementGhost = NewObject<UStaticMeshComponent>(this);
		PlacementGhost->SetUsingAbsoluteLocation(true);
		PlacementGhost->SetUsingAbsoluteRotation(true);
		PlacementGhost->SetUsingAbsoluteScale(true);
		PlacementGhost->SetupAttachment(GetRootComponent());
		PlacementGhost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PlacementGhost->SetCastShadow(false);
		PlacementGhost->RegisterComponent();
	}
	FTransform Fit;
	UStaticMesh* GhostMesh = bBhurak ? ABhurak::Mesh(Fit, static_cast<uint8>(FMath::Max(0, ABhurak::CoatOfItem(HandItem)))) : bCart ? ErewhonPlaceables::CartMesh(Fit) : ErewhonPlaceables::StorageMesh(Kind, Fit);
	if (!GhostMesh)
	{
		GhostMesh = GetShape(EShape::Cube);
		FVector Size = bCart ? FVector(260.f, 100.f, 80.f) : FVector(60.f, 40.f, 36.f);
		if (!bCart && Kind == EStorageKind::Shelf)
		{
			Size = FVector(36.f, 120.f, 190.f);
		}
		else if (!bCart && Kind == EStorageKind::Table)
		{
			Size = FVector(84.f, 190.f, 82.f);
		}
		else if (!bCart && Kind == EStorageKind::DryingRack)
		{
			Size = FVector(50.f, 150.f, 170.f);
		}
		else if (!bCart && Kind == EStorageKind::Workbench)
		{
			Size = FVector(70.f, 160.f, 120.f);
		}
		else if (!bCart && Kind == EStorageKind::Potpourri)
		{
			Size = FVector(28.f, 28.f, 14.f);
		}
		else if (!bCart && Kind == EStorageKind::CauldronStand)
		{
			Size = FVector(70.f, 70.f, 150.f);
		}
		else if (!bCart && Kind == EStorageKind::LogPile)
		{
			Size = FVector(120.f, 210.f, 40.f);
		}
		Fit = FTransform(FRotator::ZeroRotator, FVector(0, 0, Size.Z * 0.5f), Size / 100.f);
	}
	if (PlacementGhost->GetStaticMesh() != GhostMesh)
	{
		PlacementGhost->SetStaticMesh(GhostMesh);
		if (!HighlightMaterial)
		{
			HighlightMaterial = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Highlight"));
		}
		for (int32 Slot = 0; Slot < PlacementGhost->GetNumMaterials(); ++Slot)
		{
			PlacementGhost->SetMaterial(Slot, HighlightMaterial);
		}
	}
	PlacementGhost->SetWorldTransform(Fit * At);
	PlacementGhost->SetVisibility(true);
	// Rouge si l'on ne peut pas le poser là.
	if (!ForbiddenMaterial && HighlightMaterial)
	{
		ForbiddenMaterial = UMaterialInstanceDynamic::Create(HighlightMaterial, this);
		ForbiddenMaterial->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(1.f, 0.15f, 0.1f));
	}
	for (int32 Slot = 0; Slot < PlacementGhost->GetNumMaterials(); ++Slot)
	{
		PlacementGhost->SetMaterial(Slot, Placement.bForbidden && ForbiddenMaterial ? static_cast<UMaterialInterface*>(ForbiddenMaterial) : HighlightMaterial.Get());
	}
}

void AErewhonCharacter::UpdateHighlight()
{
	// La plante qu'on peut récolter s'entoure d'un halo chaud ; rien sinon.
	TArray<TPair<UStaticMesh*, FTransform>> Parts;
	// La faux en main : tout ce qu'elle couperait, plantes sauvages et cultures mûres, chez soi et à portée.
	const AErewhonPlayerState* MyState = GetPlayerState<AErewhonPlayerState>();
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (MyState && State && !IsMenuOpen() && MyState->GetInventory()->GetHandItem() == ErewhonFarm::Scythe)
	{
		USectorLayoutSubsystem* Layouts = GetWorld()->GetSubsystem<USectorLayoutSubsystem>();
		const FVector Center = GetActorLocation();
		const FIntPoint Here = ErewhonSector::FromWorld(Center);
		TArray<TPair<UStaticMesh*, FTransform>> One;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const FIntPoint Coord = Here + FIntPoint(DX, DY);
				const FString* OwnerKey = State->FindSectorOwnerKey(Coord);
				const ASector* Sector = OwnerKey && *OwnerKey == MyState->GetPlayerKey() ? ASectorManager::FindLoaded(this, Coord) : nullptr;
				if (!Sector)
				{
					continue;
				}
				const TSharedRef<const FSectorLayout> Layout = Layouts->GetLayout(Coord, State->GetWorldSeed());
				TArray<int32> Near;
				Layout->GatherNear(Center, ErewhonFarm::ScytheRadius, Near);
				for (const int32 Index : Near)
				{
					if (!Layout->CoveredPlants.Contains(Index) && !State->IsHarvested(Coord, Index))
					{
						Sector->GetPlantVisual(Index, One);
						Parts.Append(One);
					}
				}
				Sector->GetRipeCropVisuals(FVector2D(Center), ErewhonFarm::ScytheRadius, Parts);
			}
		}
	}
	else if (Target.Plant.IsValid() && Target.bPlantHarvestable)
	{
		if (const ASector* Sector = ASectorManager::FindLoaded(this, Target.Plant.Sector))
		{
			Sector->GetPlantVisual(Target.Plant.PlantIndex, Parts);
		}
	}
	if (!HighlightMaterial)
	{
		HighlightMaterial = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Highlight"));
	}
	while (Highlights.Num() < Parts.Num())
	{
		UStaticMeshComponent* Glow = NewObject<UStaticMeshComponent>(this);
		Glow->SetUsingAbsoluteLocation(true);
		Glow->SetUsingAbsoluteRotation(true);
		Glow->SetUsingAbsoluteScale(true);
		Glow->SetupAttachment(GetRootComponent());
		Glow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Glow->SetCastShadow(false);
		Glow->SetOnlyOwnerSee(true);
		Glow->RegisterComponent();
		Highlights.Add(Glow);
	}
	for (int32 i = 0; i < Highlights.Num(); ++i)
	{
		UStaticMeshComponent* Glow = Highlights[i];
		const bool bShow = i < Parts.Num() && HighlightMaterial;
		Glow->SetVisibility(bShow);
		if (!bShow)
		{
			continue;
		}
		if (Glow->GetStaticMesh() != Parts[i].Key)
		{
			Glow->SetStaticMesh(Parts[i].Key);
			for (int32 Slot = 0; Slot < Glow->GetNumMaterials(); ++Slot)
			{
				Glow->SetMaterial(Slot, HighlightMaterial);
			}
		}
		// Un peu plus grand que la plante, pour l'envelopper.
		FTransform Transform = Parts[i].Value;
		Transform.SetScale3D(Transform.GetScale3D() * 1.06f);
		Glow->SetWorldTransform(Transform);
	}
}

void AErewhonCharacter::UpdateTarget()
{
	Target = FCharacterTarget();
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const AErewhonPlayerState* MyState = GetPlayerState<AErewhonPlayerState>();
	if (!State || !MyState || IsMenuOpen())
	{
		return;
	}

	const UErewhonSettings& Settings = UErewhonSettings::Get();
	const FVector Eye = Camera->GetComponentLocation();
	const FVector Look = GetControlRotation().Vector();
	float Reach = Settings.HarvestReach();

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonTarget), false, this);
	if (Mount)
	{
		// En selle, on vise par-dessus sa monture, ses sacoches et la charrette qu'elle tire.
		TArray<AActor*> Ridden;
		Mount->GetAttachedActors(Ridden, true, true);
		Ridden.Add(Mount);
		if (ACart* Towed = Mount->GetTowedCart())
		{
			Ridden.Add(Towed);
			Towed->GetAttachedActors(Ridden, false, true);
		}
		Params.AddIgnoredActors(Ridden);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Look * Reach, ECC_Visibility, Params))
	{
		AActor* HitActor = Hit.GetActor();
		if (const IErewhonInteractable* Interactable = Cast<IErewhonInteractable>(HitActor))
		{
			if (Hit.Distance <= Settings.MerchantReach())
			{
				Target.Interactable = HitActor;
				Target.Hint = FText::Format(LOCTEXT("InteractHint", "[E] {0}"), Interactable->GetInteractionLabel());
				// Le pichet en main, devant un tonneau ou le chaudron : puiser, verser, et ce qu'il contient.
				const AStorageActor* Vessel = Cast<AStorageActor>(HitActor);
				const FName HandItem = MyState->GetInventory()->GetHandItem();
				const int32 InPitcher = ErewhonApothecary::PitcherLitersOf(HandItem);
				if (Vessel && Vessel->GetKind() == EStorageKind::Cauldron && HandItem == ErewhonApothecary::EmptyVial)
				{
					Target.Hint = ErewhonApothecary::FindBrew(Vessel->GetLiquid())
						? FText::Format(LOCTEXT("VialHint", "[Clic droit] Remplir une fiole (0,5 L) : {0}"), ErewhonApothecary::LiquidName(Vessel->GetLiquid()))
						: LOCTEXT("VialNoBrew", "Il n'y a pas de breuvage à mettre en fiole");
				}
				else if (Vessel && InPitcher >= 0 && (Vessel->GetKind() == EStorageKind::Barrel || Vessel->GetKind() == EStorageKind::Cauldron))
				{
					const FText Content = InPitcher == 0 ? LOCTEXT("PitcherEmptyHint", "vide")
						: FText::Format(LOCTEXT("PitcherHas", "{0} L de {1}"), FText::AsNumber(InPitcher), ErewhonApothecary::LiquidName(ErewhonApothecary::LiquidOfPitcher(HandItem)).ToLower());
					const AErewhonPlayerController* MyPC = GetController<AErewhonPlayerController>();
					Target.Hint = MyPC && MyPC->GetCraftJob().IsActive()
						? FText::Format(LOCTEXT("PitcherBusy", "Transvasement…  [Clic] Arrêter    (pichet : {0})"), Content)
						: FText::Format(LOCTEXT("PitcherHint", "[Clic gauche] Puiser   [Clic droit] Verser    (1 L par seconde ; pichet : {0})"), Content);
				}
				return;
			}
		}
		// Un mur ou le sol arrête la visée : pas de cueillette à travers.
		Reach = Hit.Distance + 30.f;
	}

	// En selle : descendre, atteler.
	if (Mount)
	{
		const ACart* Towed = Mount->GetTowedCart();
		if (Towed && Towed->GetCartKind() == ECartKind::Cultivator)
		{
			Target.Hint = Towed->IsEngaged() ? LOCTEXT("RideHintCultDown", "Cultivateur baissé    [Clic] Le lever    [F] Descendre    [G] Dételer")
				: LOCTEXT("RideHintCultUp", "Cultivateur levé    [Clic] Le baisser    [F] Descendre    [G] Dételer");
		}
		else if (Towed && Towed->GetCartKind() == ECartKind::Harvester)
		{
			Target.Hint = Towed->IsEngaged() ? LOCTEXT("RideHintHarvestOn", "Moissonneuse : rabatteur baissé    [Clic] Le lever    [F] Descendre    [G] Dételer")
				: LOCTEXT("RideHintHarvestOff", "Moissonneuse : rabatteur levé    [Clic] Le baisser    [F] Descendre    [G] Dételer");
		}
		else if (Towed && Towed->GetCartKind() == ECartKind::Seeder)
		{
			Target.Hint = Towed->IsEngaged() ? LOCTEXT("RideHintSeedOn", "Semoir enclenché    [Clic] Désenclencher    [F] Descendre    [G] Dételer")
				: LOCTEXT("RideHintSeedOff", "Semoir en roue libre    [Clic] Enclencher    [F] Descendre    [G] Dételer");
		}
		else
		{
			Target.Hint = Towed ? LOCTEXT("RideHintCart", "[F] Descendre    [G] Dételer la charrette")
				: LOCTEXT("RideHint", "[F] Descendre    [G] Atteler une charrette ou un outil tout proche");
		}
		return;
	}
	// Ce qu'on tient : la faux fauche tout autour, sans viser.
	const FName HandId = MyState->GetInventory()->GetHandItem();
	if (HandId == ErewhonFarm::Scythe)
	{
		Target.Hint = LOCTEXT("ScytheHint", "[Clic] Faucher : tout ce qui pousse à portée, autour de vous");
		return;
	}
	// La hache en main : le tronc d'un arbre, sous le regard.
	if (HandId == ErewhonForestry::Axe && Hit.bBlockingHit && Hit.Distance <= Settings.HarvestReach() + 100.f)
	{
		if (const ASector* TreeSector = Cast<ASector>(Hit.GetActor()))
		{
			const int32 Tree = TreeSector->TreeAtTrunk(Hit.GetComponent(), Hit.Item);
			const FIntPoint Coord = TreeSector->GetCoord();
			if (Tree != INDEX_NONE && !State->IsHarvested(Coord, ErewhonForestry::HarvestIndexOfTree(Tree)))
			{
				const FString* TreeOwner = State->FindSectorOwnerKey(Coord);
				if (TreeOwner && *TreeOwner == MyState->GetPlayerKey())
				{
					Target.TreeSector = Coord;
					Target.TreeIndex = Tree;
					Target.Hint = LOCTEXT("AxeHint", "[Clic] Un coup de hache : quelques-uns, et l'arbre tombe");
				}
				else
				{
					Target.Hint = LOCTEXT("AxeNotYours", "On n'abat que les arbres de ses secteurs");
				}
				return;
			}
		}
	}
	// Une case de son champ, sous le regard.
	if (Hit.bBlockingHit && Hit.ImpactNormal.Z > 0.5f && Hit.Distance <= Settings.HarvestReach())
	{
		const FIntPoint FieldSector = ErewhonSector::FromWorld(Hit.ImpactPoint);
		const FString* FieldOwner = State->FindSectorOwnerKey(FieldSector);
		const int32 Cell = ErewhonFarm::CellAt(FieldSector, FVector2D(Hit.ImpactPoint));
		if (FieldOwner && *FieldOwner == MyState->GetPlayerKey() && ErewhonFarm::IsFieldCell(FieldSector, State->GetBuildings(FieldSector), Cell))
		{
			Target.FieldSector = FieldSector;
			Target.FieldCell = Cell;
			const FVector2D CellMid = ErewhonFarm::CellCenter(FieldSector, Cell);
			Target.FieldCorner = (Hit.ImpactPoint.X > CellMid.X ? 1 : 0) | (Hit.ImpactPoint.Y > CellMid.Y ? 2 : 0);
			Target.Hint = FieldHint(State->FindFieldCell(FieldSector, Cell), HandId);
			return;
		}
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = PendingHarvests.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() > GPendingHarvestSeconds)
		{
			It.RemoveCurrent();
		}
	}

	// La cerpe cible dans un cône horizontal ; à main nue, on vise au réticule.
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* Hand = Items ? Items->Find(MyState->GetInventory()->GetHandItem()) : nullptr;
	const float Cone = Hand && Hand->bHarvestTool ? Settings.SickleConeDegrees : 0.f;

	USectorLayoutSubsystem* Layouts = GetWorld()->GetSubsystem<USectorLayoutSubsystem>();
	Target.Plant = Layouts->FindTarget(Eye, Look, Cone > 0.f ? Settings.HarvestReach() : Reach, Cone, State->GetWorldSeed(),
		[this, State](const FIntPoint& Sector, int32 PlantIndex)
		{
			return !State->IsHarvested(Sector, PlantIndex) && !PendingHarvests.Contains({ Sector, PlantIndex });
		});
	if (!Target.Plant.IsValid())
	{
		return;
	}

	const FString* OwnerKey = State->FindSectorOwnerKey(Target.Plant.Sector);
	Target.bPlantHarvestable = OwnerKey && *OwnerKey == MyState->GetPlayerKey();
	const FItemDef* PlantDef = Items ? Items->Find(Target.Plant.ItemId) : nullptr;
	const FText PlantName = PlantDef ? PlantDef->Name : FText::FromName(Target.Plant.ItemId);
	Target.Hint = Target.bPlantHarvestable
		? FText::Format(LOCTEXT("HarvestHint", "[Clic] Récolter : {0}"), PlantName)
		: FText::Format(LOCTEXT("NotOwnedHint", "{0} — secteur non possédé"), PlantName);
}

float AErewhonCharacter::GetCarriedWeight() const
{
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	if (!State)
	{
		return 0.f;
	}
	const UBackpackComponent* Bag = State->GetBackpack();
	return State->GetInventory()->GetTotalWeight() + (Bag && Bag->IsWorn() ? Bag->GetTotalWeight() : 0.f);
}

float AErewhonCharacter::GetCarryComfort() const
{
	// En selle, c'est le bhurak qui porte ; la potion de force double ce qu'on supporte.
	return UErewhonSettings::Get().CarryComfortKg * (Mount ? 4.f : 1.f) * (HasPotion(EPotionEffect::Strength) ? 2.f : 1.f);
}

float AErewhonCharacter::GetCarryMax() const
{
	return UErewhonSettings::Get().CarryMaxKg * (Mount ? 4.f : 1.f) * (HasPotion(EPotionEffect::Strength) ? 2.f : 1.f);
}

void AErewhonCharacter::OnRep_Mount()
{
	// En selle : une capsule plus large (le corps de la bête), le regard à hauteur de cavalier.
	const float Rise = Mount ? ABhurak::RideRise : 0.f;
	GetCapsuleComponent()->SetCapsuleRadius(Mount ? 70.f : 32.f);
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 66.f + Rise));
	CameraArm->SetRelativeLocation(FVector(0.f, 0.f, 62.f + Rise));
	if (AnimatedBody && !Mount)
	{
		AnimatedBody->SetRelativeRotation(FRotator(0.f, GBodyMeshYaw, 0.f));
	}
}

void AErewhonCharacter::Secondary()
{
	// Clic droit, le pichet en main devant un tonneau ou le chaudron : verser.
	AErewhonPlayerController* PC = GetController<AErewhonPlayerController>();
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	if (!PC || !State || PC->IsMenuOpen())
	{
		return;
	}
	AStorageActor* Vessel = Cast<AStorageActor>(Target.Interactable.Get());
	// Une fiole vide en main devant le chaudron : on la remplit d'un demi-litre de breuvage, c'est une potion.
	if (Vessel && Vessel->GetKind() == EStorageKind::Cauldron && State->GetInventory()->GetHandItem() == ErewhonApothecary::EmptyVial)
	{
		if (!State->IsApothecary())
		{
			PC->ShowToast(LOCTEXT("ApothecaryOnly", "Réservé aux apothicaires : choisissez cette classe à la mairie."));
			return;
		}
		PC->ServerStartPour(Vessel, 2, NAME_None, 1);
		return;
	}
	TryPitcher(Vessel, false);
}

bool AErewhonCharacter::TryPitcher(AStorageActor* Storage, bool bDraw)
{
	AErewhonPlayerController* PC = GetController<AErewhonPlayerController>();
	const AErewhonPlayerState* State = GetPlayerState<AErewhonPlayerState>();
	if (!PC || !State || !Storage || ErewhonApothecary::PitcherLitersOf(State->GetInventory()->GetHandItem()) < 0
		|| (Storage->GetKind() != EStorageKind::Barrel && Storage->GetKind() != EStorageKind::Cauldron) || !Storage->IsOwnedBy(State->GetPlayerKey()))
	{
		return false;
	}
	if (Storage->GetKind() == EStorageKind::Cauldron && !State->IsApothecary())
	{
		PC->ShowToast(LOCTEXT("ApothecaryOnly", "Réservé aux apothicaires : choisissez cette classe à la mairie."));
		return true;
	}
	// Déjà en train de transvaser : la touche arrête (le litre entamé ne compte pas).
	if (PC->GetCraftJob().IsActive())
	{
		PC->ServerStopCraft();
		return true;
	}
	PC->ServerStartPour(Storage, bDraw ? 5 : 6, NAME_None, 1);
	return true;
}

void AErewhonCharacter::PlayToolSound(const TCHAR* SoundName, float Volume)
{
	if (!ToolAudio)
	{
		ToolAudio = NewObject<UAudioComponent>(this);
		ToolAudio->SetupAttachment(GetCapsuleComponent());
		ToolAudio->SetRelativeLocation(FVector(40.f, 20.f, 30.f));
		ToolAudio->bAutoActivate = false;
		ToolAudio->bAllowSpatialization = false;
		ToolAudio->RegisterComponent();
	}
	if (USoundWave* Sound = ErewhonSound::Load(SoundName))
	{
		ToolAudio->Stop();
		ToolAudio->SetSound(Sound);
		ToolAudio->SetVolumeMultiplier(Volume * ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Effects));
		ToolAudio->SetPitchMultiplier(FMath::FRandRange(0.93f, 1.07f));
		ToolAudio->Play();
	}
}

FText AErewhonCharacter::FieldHint(const FFieldCellEntry* Entry, FName Hand) const
{
	using namespace ErewhonFarm;
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const auto NameOf = [Items](FName Id)
	{
		const FItemDef* Def = Items ? Items->Find(Id) : nullptr;
		return Def ? Def->Name : FText::FromName(Id);
	};
	if (Hand == Spade)
	{
		return Entry ? LOCTEXT("FieldTilled", "Terre retournée") : LOCTEXT("FieldTill", "[Clic] Retourner la terre");
	}
	if (const int32 Seed = CropOfSeed(Hand); Seed != INDEX_NONE)
	{
		if (!Entry)
		{
			return LOCTEXT("FieldTillFirst", "Terre grossière : retournez-la d'abord à la bêche");
		}
		if (Entry->Crop == 0)
		{
			return FText::Format(LOCTEXT("FieldSow", "[Clic] Semer : {0}"), NameOf(Crops()[Seed].Product));
		}
	}
	if (!Entry)
	{
		return LOCTEXT("FieldRaw", "Terre grossière : à retourner à la bêche (General Store)");
	}
	if (Entry->Crop == 0 || !Crops().IsValidIndex(Entry->Crop - 1))
	{
		return LOCTEXT("FieldReady", "Terre retournée : semez-y des graines (General Store)");
	}
	const FText Product = NameOf(Crops()[Entry->Crop - 1].Product);
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const double Now = State ? State->GetWorldClock() : 0.0;
	const int32 Grown = Stage(Entry->SownAt, Now);
	if (Grown >= RipeStage)
	{
		return Hand.IsNone() || Hand == Sickle ? FText::Format(LOCTEXT("FieldHarvest", "[Clic] Récolter : {0}"), Product)
			: FText::Format(LOCTEXT("FieldRipe", "{0} : mûr (à la main, à la cerpe ou à la faux)"), Product);
	}
	const int32 Minutes = FMath::Max(1, FMath::CeilToInt32((Entry->SownAt + DaySeconds() * RipeStage - Now) / 60.0));
	const FText StageText = Grown == 0 ? LOCTEXT("StageSown", "semé, rien ne paraît encore")
		: Grown == 1 ? LOCTEXT("StageSprouts", "petites pousses") : LOCTEXT("StageTall", "grandes pousses");
	return FText::Format(LOCTEXT("FieldGrowing", "{0} : {1} — mûr dans {2} min"), Product, StageText, Minutes);
}

#undef LOCTEXT_NAMESPACE
