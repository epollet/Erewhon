#include "Player/ErewhonCharacter.h"

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
#include "World/Sector.h"
#include "World/SectorManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "ErewhonCharacter"

using namespace ErewhonVisuals;

namespace
{
	constexpr float GSwingDuration = 0.35f;
	constexpr double GPendingHarvestSeconds = 1.0;
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
	if (CurrentAnimation != TEXT("sit") && CurrentAnimation != TEXT("jump"))
	{
		const FVector Hips = AnimatedBody->GetSocketTransform(HipsBone, RTS_Component).GetLocation();
		Offset = AnimatedBody->GetRelativeRotation().RotateVector(FVector(Hips.X, Hips.Y, 0.f));
	}
	AnimatedBody->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) - Offset);
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
			const FTransform Fit = FitToHeight(HandMesh, 45.f);
			const FVector Base = Held == HeldFirstPerson.Get() ? GHeldFirstPersonOffset : (AnimatedBody ? HeldThirdPersonBase : GHeldThirdPersonOffset);
			Held->SetStaticMesh(HandMesh);
			Held->SetRelativeLocation(Base + Held->GetRelativeRotation().RotateVector(Fit.GetLocation()));
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
	Input->BindAction(Actions.Use, ETriggerEvent::Started, this, &AErewhonCharacter::Use);
	Input->BindAction(Actions.Crouch, ETriggerEvent::Started, this, &AErewhonCharacter::StartCrouch);
	Input->BindAction(Actions.Crouch, ETriggerEvent::Completed, this, &AErewhonCharacter::StopCrouch);
	Input->BindAction(Actions.Inventory, ETriggerEvent::Started, this, &AErewhonCharacter::ToggleInventory);
	Input->BindAction(Actions.Bag, ETriggerEvent::Started, this, &AErewhonCharacter::BagAction);
	Input->BindAction(Actions.Stash, ETriggerEvent::Started, this, &AErewhonCharacter::StashAction);
	Input->BindAction(Actions.View, ETriggerEvent::Started, this, &AErewhonCharacter::CycleView);
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
	if ((DrivenCart || (Carried && ErewhonPlaceables::IsHeavy(Carried->GetKind()))) && !HasPotion(EPotionEffect::Strength))
	{
		Factor *= 0.5f;   // on pousse une charrette, on porte un tonneau (sauf avec la potion de force)
	}
	if (HasPotion(EPotionEffect::Speed))
	{
		Factor *= 1.6f;
	}
	const float Base = bIsRunning ? Settings.RunSpeedMetersPerSecond : Settings.WalkSpeedMetersPerSecond;
	GetCharacterMovement()->MaxWalkSpeed = Base * Factor * 100.f;
	GetCharacterMovement()->MaxWalkSpeedCrouched = Settings.WalkSpeedMetersPerSecond * 0.5f * Factor * 100.f;
}

void AErewhonCharacter::ServerSetRunning_Implementation(bool bRunning)
{
	ApplyRunning(bRunning);
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
	if (AStorageActor* Storage = Cast<AStorageActor>(Target.Interactable.Get()))
	{
		if (Storage->IsOwnedBy(State->GetPlayerKey()))
		{
			// Le mortier et le chaudron servent à préparer ; la table ne s'ouvre pas.
			switch (Storage->GetKind())
			{
			case EStorageKind::Mortar:
			case EStorageKind::Cauldron:
			case EStorageKind::Workbench:
				PC->OpenApothecary(Storage, static_cast<uint8>(Storage->GetKind() == EStorageKind::Cauldron ? ErewhonApothecary::ECraft::Cauldron
					: Storage->GetKind() == EStorageKind::Workbench ? ErewhonApothecary::ECraft::Workbench : ErewhonApothecary::ECraft::Mortar));
				break;
			case EStorageKind::Potpourri:
				break;   // il parfume, c'est tout
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
		PC->ServerToggleDrive(Cart);
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
	if (IErewhonInteractable* Interactable = Cast<IErewhonInteractable>(Target.Interactable.Get()))
	{
		Interactable->Interact(PC);
		return;
	}

	// Un outil en main : chaque clic donne son coup (et son bruit), repris depuis le début s'il était en cours.
	const AErewhonPlayerState* MyState = GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* HandDef = MyState && Items ? Items->Find(MyState->GetInventory()->GetHandItem()) : nullptr;
	if (HandDef && HandDef->Category == EItemCategory::Tool)
	{
		SwingTime = GSwingDuration;
		if (!ToolAudio)
		{
			ToolAudio = NewObject<UAudioComponent>(this);
			ToolAudio->SetupAttachment(GetCapsuleComponent());
			ToolAudio->SetRelativeLocation(FVector(40.f, 20.f, 30.f));
			ToolAudio->bAutoActivate = false;
			ToolAudio->bAllowSpatialization = false;
			ToolAudio->RegisterComponent();
		}
		if (USoundWave* Swish = ErewhonSound::Load(TEXT("sickle_swish")))
		{
			ToolAudio->Stop();
			ToolAudio->SetSound(Swish);
			ToolAudio->SetVolumeMultiplier(0.7f * ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Effects));
			ToolAudio->SetPitchMultiplier(FMath::FRandRange(0.93f, 1.07f));
			ToolAudio->Play();
		}
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
	UpdateTarget();
	UpdateHighlight();
	UpdatePlacement();

	// Coup de l'objet tenu : un aller-retour vers le bas.
	if (SwingTime > 0.f)
	{
		SwingTime = FMath::Max(0.f, SwingTime - DeltaSeconds);
		const float Progress = 1.f - SwingTime / GSwingDuration;
		const float Arc = FMath::Sin(Progress * UE_PI) * 55.f;
		HeldFirstPerson->SetRelativeRotation(GHeldFirstPersonRotation + FRotator(-Arc, -Arc * 0.4f, 0.f));
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

void AErewhonCharacter::DrinkPotion(EPotionEffect Effect)
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
	PotionUntil[static_cast<int32>(Effect)] = State->GetWorldClock() + ErewhonApothecary::PotionSeconds;
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
	if (!Movement->IsMovingOnGround() || Speed < 40.f)
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
	return Hand && Hand->IsPlaceable();
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
	const bool bCart = !Carried && ErewhonPlaceables::IsCart(HandItem);
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
	UStaticMesh* GhostMesh = bCart ? ErewhonPlaceables::CartMesh(Fit) : ErewhonPlaceables::StorageMesh(Kind, Fit);
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
	if (Target.Plant.IsValid() && Target.bPlantHarvestable)
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
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Look * Reach, ECC_Visibility, Params))
	{
		AActor* HitActor = Hit.GetActor();
		if (const IErewhonInteractable* Interactable = Cast<IErewhonInteractable>(HitActor))
		{
			if (Hit.Distance <= Settings.MerchantReach())
			{
				Target.Interactable = HitActor;
				Target.Hint = FText::Format(LOCTEXT("InteractHint", "[E] {0}"), Interactable->GetInteractionLabel());
				return;
			}
		}
		// Un mur ou le sol arrête la visée : pas de cueillette à travers.
		Reach = Hit.Distance + 30.f;
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

#undef LOCTEXT_NAMESPACE
