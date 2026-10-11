#include "Player/ErewhonCharacter.h"

#include "Camera/CameraComponent.h"
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

#define LOCTEXT_NAMESPACE "ErewhonCharacter"

using namespace ErewhonVisuals;

namespace
{
	constexpr float GSwingDuration = 0.35f;
	constexpr double GPendingHarvestSeconds = 1.0;
	const FVector GHeldFirstPersonOffset(38.f, 24.f, -22.f);
	const FRotator GHeldFirstPersonRotation(-20.f, 0.f, 10.f);
	const FVector GHeldThirdPersonOffset(22.f, 28.f, 0.f);
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
	BindAppearance();
}

void AErewhonCharacter::BuildBody()
{
	// Silhouette provisoire, vue par les autres joueurs uniquement.
	USceneComponent* Capsule = GetCapsuleComponent();
	Legs = AddShape(this, Capsule, EShape::Cylinder, FVector(0, 0, -50), FVector(34, 28, 76), Palette::DefaultShorts, FRotator::ZeroRotator, false);
	Torso = AddShape(this, Capsule, EShape::Cylinder, FVector(0, 0, 18), FVector(42, 30, 62), Palette::Skin, FRotator::ZeroRotator, false);
	UStaticMeshComponent* Head = AddShape(this, Capsule, EShape::Sphere, FVector(0, 0, 66), FVector(30, 30, 32), Palette::Skin, FRotator::ZeroRotator, false);
	for (UStaticMeshComponent* Part : { Legs.Get(), Torso.Get(), Head })
	{
		Part->SetOwnerNoSee(true);
	}
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
		Held->SetVisibility(Hand != nullptr);
		if (!Hand)
		{
			continue;
		}
		if (UStaticMesh* HandMesh = Cast<UStaticMesh>(Hand->Mesh.TryLoad()))
		{
			// Modèle généré : ramené à ~45 cm, la base (le manche) au creux de la main.
			const FTransform Fit = FitToHeight(HandMesh, 45.f);
			const FVector Base = Held == HeldFirstPerson.Get() ? GHeldFirstPersonOffset : GHeldThirdPersonOffset;
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

	if (IErewhonInteractable* Interactable = Cast<IErewhonInteractable>(Target.Interactable.Get()))
	{
		Interactable->Interact(PC);
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
	if (!IsLocallyControlled())
	{
		return;
	}
	UpdateTarget();

	// Coup de l'objet tenu : un aller-retour vers le bas.
	if (SwingTime > 0.f)
	{
		SwingTime = FMath::Max(0.f, SwingTime - DeltaSeconds);
		const float Progress = 1.f - SwingTime / GSwingDuration;
		const float Arc = FMath::Sin(Progress * UE_PI) * 55.f;
		HeldFirstPerson->SetRelativeRotation(GHeldFirstPersonRotation + FRotator(-Arc, -Arc * 0.4f, 0.f));
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
	const FVector Look = Camera->GetForwardVector();
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
