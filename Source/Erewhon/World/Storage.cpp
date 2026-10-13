#include "World/Storage.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Items/ItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "World/ErewhonVisuals.h"

#define LOCTEXT_NAMESPACE "ErewhonStorage"

using namespace ErewhonVisuals;

namespace
{
	/** Le modèle généré de la charrette a son grand côté sur Y : un quart de tour l'aligne sur X, brancards vers −X. */
	constexpr float GCartMeshYaw = -90.f;
	constexpr float GCartLength = 260.f;

	/** Caisse sans roues (les roues sont des pièces à part, qui tournent) : même règle d'orientation. */
	constexpr float GBodyYaw = 90.f;
	constexpr float GBodyLength = 230.f;
	/** Décalage de la caisse pour que son moyeu tombe sur l'essieu des roues. */
	const FVector GBodyOffset(-14.f, 0.f, 14.f);

	const FString& LocalPlayerKey(const UObject* Context)
	{
		static const FString None;
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
		const AErewhonPlayerState* State = Local ? Local->GetPlayerState<AErewhonPlayerState>() : nullptr;
		return State ? State->GetPlayerKey() : None;
	}
}

namespace ErewhonPlaceables
{
	TOptional<EStorageKind> StorageForItem(FName ItemId)
	{
		if (ItemId == TEXT("crate"))
		{
			return EStorageKind::Crate;
		}
		if (ItemId == TEXT("barrel"))
		{
			return EStorageKind::Barrel;
		}
		return {};
	}

	FName ItemForStorage(EStorageKind Kind)
	{
		return Kind == EStorageKind::Barrel ? FName(TEXT("barrel")) : FName(TEXT("crate"));
	}

	bool IsCart(FName ItemId)
	{
		return ItemId == TEXT("cart");
	}

	int32 Capacity(EStorageKind Kind)
	{
		// Le tonneau contient deux cagettes et demie.
		return Kind == EStorageKind::Barrel ? 30 : 12;
	}

	float Height(EStorageKind Kind)
	{
		return Kind == EStorageKind::Barrel ? 95.f : 36.f;
	}

	FText KindName(EStorageKind Kind)
	{
		return Kind == EStorageKind::Barrel ? LOCTEXT("Barrel", "Tonneau") : LOCTEXT("Crate", "Cagette");
	}

	UStaticMesh* StorageMesh(EStorageKind Kind, FTransform& OutFit)
	{
		// La cagette est construite en lattes (le modèle généré n'était qu'une façade plate).
		if (Kind == EStorageKind::Crate)
		{
			return nullptr;
		}
		UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_barrel"));
		if (Mesh)
		{
			OutFit = Kind == EStorageKind::Barrel ? FitToHeight(Mesh, Height(Kind)) : FitToLength(Mesh, 60.f, Height(Kind));
		}
		return Mesh;
	}

	UStaticMesh* CartMesh(FTransform& OutFit)
	{
		// La caisse sans roues d'abord ; à défaut, l'ancien modèle d'un seul tenant.
		if (UStaticMesh* Body = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cart_body")))
		{
			OutFit = FitToLength(Body, GBodyLength) * FTransform(FRotator(0.f, GBodyYaw, 0.f), GBodyOffset);
			return Body;
		}
		UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cart"));
		if (Mesh)
		{
			OutFit = FitToLength(Mesh, GCartLength) * FTransform(FRotator(0.f, GCartMeshYaw, 0.f), FVector(-20.f, 0.f, 0.f));
		}
		return Mesh;
	}
}

// ── Conteneurs ───────────────────────────────────────────────────────────────

AStorageActor::AStorageActor()
{
	bReplicates = true;
	SetReplicatingMovement(true);   // les attaches (mains, pile, charrette) suivent sur les clients
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AStorageActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AStorageActor, Kind);
	DOREPLIFETIME(AStorageActor, OwnerKey);
	DOREPLIFETIME(AStorageActor, OwnerName);
	DOREPLIFETIME(AStorageActor, Slots);
	DOREPLIFETIME(AStorageActor, Carrier);
	DOREPLIFETIME(AStorageActor, Cart);
	DOREPLIFETIME(AStorageActor, CartSlot);
	DOREPLIFETIME(AStorageActor, Below);
	DOREPLIFETIME(AStorageActor, Above);
}

void AStorageActor::Setup(EStorageKind InKind, const FString& InOwnerKey, const FString& InOwnerName)
{
	Kind = InKind;
	OwnerKey = InOwnerKey;
	OwnerName = InOwnerName;
	Slots.SetNum(ErewhonPlaceables::Capacity(Kind));
}

void AStorageActor::BeginPlay()
{
	Super::BeginPlay();
	FTransform Fit;
	if (UStaticMesh* Mesh = ErewhonPlaceables::StorageMesh(Kind, Fit))
	{
		Body = NewObject<UStaticMeshComponent>(this);
		Body->SetStaticMesh(Mesh);
		Body->SetupAttachment(Root);
		Body->SetRelativeTransform(Fit);
		Body->RegisterComponent();
		AddInstanceComponent(Body);
	}
	else if (Kind == EStorageKind::Barrel)
	{
		Body = AddShape(this, Root, EShape::Cylinder, FVector(0, 0, 47.5f), FVector(60, 60, 95), Palette::WoodLight);
		Body->SetMaterial(0, MakePaintedMaterial(Body, TEXT("wood_planks"), 80.f, Palette::WoodLight));
	}
	else
	{
		// Cagette ouverte : un fond, trois lattes par côté avec des jours entre elles, des montants
		// aux coins et une poignée découpée sur les petits côtés. 60 × 40 × 36 cm.
		const FLinearColor Slat = FLinearColor::FromSRGBColor(FColor(214, 178, 128));
		UMaterialInterface* Wood = MakePaintedMaterial(this, TEXT("wood_planks"), 60.f, Slat, FLinearColor(1.15f, 1.1f, 1.f));
		const auto Plank = [this, Wood, &Slat](const FVector& Center, const FVector& Size, bool bSolid)
		{
			UStaticMeshComponent* Part = AddShape(this, Root, EShape::Cube, Center, Size, Slat, FRotator::ZeroRotator, bSolid);
			Part->SetMaterial(0, Wood);
			return Part;
		};
		constexpr float L = 60.f, W = 40.f, H = 36.f, T = 1.6f;
		Plank(FVector(0, 0, 1.f), FVector(L, W, 2.f), false);   // le fond
		for (const float Z : { 6.f, 17.f, 28.f })
		{
			for (const float Side : { -1.f, 1.f })
			{
				Plank(FVector(0, Side * (W * 0.5f - T * 0.5f), Z), FVector(L, T, 8.f), false);              // grands côtés
				if (Z < 25.f)
				{
					Plank(FVector(Side * (L * 0.5f - T * 0.5f), 0, Z), FVector(T, W, 8.f), false);          // petits côtés
				}
			}
		}
		for (const float Side : { -1.f, 1.f })
		{
			// La latte du haut des petits côtés : deux morceaux autour de la poignée.
			for (const float Half : { -1.f, 1.f })
			{
				Plank(FVector(Side * (L * 0.5f - T * 0.5f), Half * 13.5f, 30.f), FVector(T, 13.f, 12.f), false);
			}
			for (const float Corner : { -1.f, 1.f })
			{
				Plank(FVector(Side * (L * 0.5f - 2.5f), Corner * (W * 0.5f - 2.5f), H * 0.5f), FVector(4.f, 4.f, H), false);
			}
		}
		// Une boîte invisible donne au tout sa collision (ciblage, empilement).
		UStaticMeshComponent* Hull = AddShape(this, Root, EShape::Cube, FVector(0, 0, H * 0.5f), FVector(L, W, H), Slat, FRotator::ZeroRotator, true);
		Hull->SetHiddenInGame(true);
		Body = Hull;
	}
	ApplyCollision();
}

void AStorageActor::ApplyCollision()
{
	// Porté, il ne doit pas bousculer son porteur.
	if (Body)
	{
		Body->SetCollisionProfileName(Carrier ? UCollisionProfile::NoCollision_ProfileName : UCollisionProfile::BlockAll_ProfileName);
	}
}

void AStorageActor::OnRep_State()
{
	ApplyCollision();
}

AStorageActor* AStorageActor::TopOfStack()
{
	AStorageActor* Top = this;
	while (Top->Above)
	{
		Top = Top->Above;
	}
	return Top;
}

int32 AStorageActor::StackSize() const
{
	const AStorageActor* Bottom = this;
	while (Bottom->Below)
	{
		Bottom = Bottom->Below;
	}
	int32 Size = 1;
	for (const AStorageActor* It = Bottom; It->Above; It = It->Above)
	{
		++Size;
	}
	return Size;
}

bool AStorageActor::Accepts(const FItemDef& Def) const
{
	return Def.Category == EItemCategory::Plant;
}

int32 AStorageActor::CountStored(FName ItemId) const
{
	int32 Total = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		Total += Slot.ItemId == ItemId ? Slot.Count : 0;
	}
	return Total;
}

int32 AStorageActor::StoreItem(FName ItemId, int32 Count)
{
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* Def = Items ? Items->Find(ItemId) : nullptr;
	if (!Def || !Accepts(*Def) || Count <= 0)
	{
		return 0;
	}
	int32 Remaining = Count;
	for (const bool bEmptyPass : { false, true })
	{
		for (FInventorySlot& Slot : Slots)
		{
			if (Remaining == 0 || Slot.IsEmpty() != bEmptyPass || (!bEmptyPass && Slot.ItemId != ItemId))
			{
				continue;
			}
			const int32 Moved = FMath::Min(Def->MaxStack - (bEmptyPass ? 0 : Slot.Count), Remaining);
			if (Moved > 0)
			{
				Slot.ItemId = ItemId;
				Slot.Count = (bEmptyPass ? 0 : Slot.Count) + Moved;
				Remaining -= Moved;
			}
		}
	}
	if (Remaining != Count)
	{
		Commit();
	}
	return Count - Remaining;
}

void AStorageActor::Release()
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (Below)
	{
		Below->Above = nullptr;
		Below = nullptr;
	}
	if (Cart)
	{
		Cart->SetLoad(CartSlot, nullptr);
		Cart = nullptr;
		CartSlot = INDEX_NONE;
	}
	Carrier = nullptr;
	ApplyCollision();
}

void AStorageActor::PlaceOnGround(const FVector& Location, float Yaw)
{
	Release();
	SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));
}

bool AStorageActor::StackOn(AStorageActor* Base)
{
	// Une cagette sur une cagette, trois au plus.
	if (!Base || Base == this || Kind != EStorageKind::Crate || Base->GetKind() != EStorageKind::Crate)
	{
		return false;
	}
	Base = Base->TopOfStack();
	if (Base->StackSize() >= ErewhonPlaceables::MaxStack || Base == this)
	{
		return false;
	}
	Release();
	AttachToActor(Base, FAttachmentTransformRules::KeepRelativeTransform);
	SetActorRelativeLocation(FVector(0.f, 0.f, ErewhonPlaceables::Height(Base->GetKind())));
	SetActorRelativeRotation(FRotator(0.f, FMath::RandRange(-4.f, 4.f), 0.f));
	Below = Base;
	Base->Above = this;
	return true;
}

bool AStorageActor::LoadInto(ACart* InCart, int32 Slot)
{
	if (!InCart || Slot < 0 || Slot >= ACart::SlotCount || InCart->GetLoad(Slot))
	{
		return false;
	}
	Release();
	AttachToActor(InCart, FAttachmentTransformRules::KeepRelativeTransform);
	SetActorRelativeTransform(ACart::SlotTransform(Slot));
	Cart = InCart;
	CartSlot = Slot;
	InCart->SetLoad(Slot, this);
	return true;
}

void AStorageActor::CarryBy(APawn* Pawn)
{
	Release();
	// Devant soi, à hauteur des hanches ; le tonneau, plus grand, descend un peu.
	AttachToActor(Pawn, FAttachmentTransformRules::KeepRelativeTransform);
	SetActorRelativeLocation(Kind == EStorageKind::Barrel ? FVector(62.f, 0.f, -62.f) : FVector(55.f, 0.f, -30.f));
	SetActorRelativeRotation(FRotator(0.f, 90.f, 0.f));
	Carrier = Pawn;
	ApplyCollision();
}

FText AStorageActor::GetInteractionLabel() const
{
	if (IsOwnedBy(LocalPlayerKey(this)))
	{
		return FText::Format(LOCTEXT("Mine", "Prendre : {0}    [F] Ouvrir"), ErewhonPlaceables::KindName(Kind));
	}
	return FText::Format(LOCTEXT("Theirs", "{0} de {1}"), ErewhonPlaceables::KindName(Kind), FText::FromString(OwnerName));
}

void AStorageActor::Interact(AErewhonPlayerController* Controller)
{
	if (Controller)
	{
		Controller->ServerPickUpStorage(this);
	}
}

// ── Charrette ────────────────────────────────────────────────────────────────

ACart::ACart()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Loads.SetNum(SlotCount);
}

void ACart::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACart, OwnerKey);
	DOREPLIFETIME(ACart, OwnerName);
	DOREPLIFETIME(ACart, Driver);
	DOREPLIFETIME(ACart, Loads);
}

void ACart::Setup(const FString& InOwnerKey, const FString& InOwnerName)
{
	OwnerKey = InOwnerKey;
	OwnerName = InOwnerName;
}

FTransform ACart::SlotTransform(int32 Slot)
{
	// Deux places sur la plate-forme, l'une devant l'autre (repère : brancards vers −X).
	return FTransform(FRotator(0.f, 90.f, 0.f), FVector(Slot == 0 ? 62.f : 0.f, 0.f, 59.f));
}

void ACart::BeginPlay()
{
	Super::BeginPlay();
	FTransform Fit;
	if (UStaticMesh* Mesh = ErewhonPlaceables::CartMesh(Fit))
	{
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(this);
		Body->SetStaticMesh(Mesh);
		Body->SetupAttachment(Root);
		Body->SetRelativeTransform(Fit);
		Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Body->RegisterComponent();
		AddInstanceComponent(Body);

		// Les deux roues, à part, sur l'essieu : chacune tourne autour de l'axe Y de son pivot.
		UStaticMesh* WheelMesh = Mesh->GetName() == TEXT("SM_cart_body") ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cart_wheel")) : nullptr;
		if (WheelMesh)
		{
			// Le disque de la roue doit se trouver dans le plan XZ : on le tourne s'il est dans le plan YZ.
			const FVector Size = WheelMesh->GetBoundingBox().GetSize();
			const float DiskYaw = Size.X < Size.Y ? 90.f : 0.f;
			const FTransform WheelFit = FitToHeight(WheelMesh, WheelDiameter) * FTransform(FRotator(0.f, DiskYaw, 0.f), FVector(0.f, 0.f, -WheelDiameter * 0.5f));
			for (const float Side : { -1.f, 1.f })
			{
				USceneComponent* Pivot = NewObject<USceneComponent>(this);
				Pivot->SetupAttachment(Root);
				Pivot->SetRelativeLocation(FVector(AxleX, Side * WheelHalfTrack, AxleZ));
				Pivot->RegisterComponent();
				AddInstanceComponent(Pivot);
				UStaticMeshComponent* Wheel = NewObject<UStaticMeshComponent>(this);
				Wheel->SetStaticMesh(WheelMesh);
				Wheel->SetupAttachment(Pivot);
				Wheel->SetRelativeTransform(WheelFit);
				Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Wheel->RegisterComponent();
				AddInstanceComponent(Wheel);
				WheelPivots.Add(Pivot);
				LastWheelPositions.Add(Pivot->GetComponentLocation());
				WheelAngles.Add(0.f);
			}
		}
		return;
	}
	// Repli : une plate-forme de planches, deux roues, deux brancards.
	const auto Wood = [this](const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator, EShape Shape = EShape::Cube)
	{
		UStaticMeshComponent* Part = AddShape(this, Root, Shape, Center, Size, Palette::WoodLight, Rotation, true);
		Part->SetMaterial(0, MakePaintedMaterial(Part, TEXT("wood_planks"), 90.f, Palette::WoodLight));
	};
	Wood(FVector(30, 0, 58), FVector(140, 92, 8));
	for (const float Y : { -46.f, 46.f })
	{
		Wood(FVector(30, Y, 72), FVector(140, 4, 24));
		Wood(FVector(30, Y * 1.18f, 40), FVector(80, 80, 6), FRotator(0.f, 0.f, 90.f), EShape::Cylinder);
		Wood(FVector(-90, Y * 0.75f, 64), FVector(130, 6, 6));
	}
}

FText ACart::GetInteractionLabel() const
{
	if (IsOwnedBy(LocalPlayerKey(this)))
	{
		return LOCTEXT("CartMine", "Charrette    [F] La conduire (devant les brancards)");
	}
	return FText::Format(LOCTEXT("CartTheirs", "Charrette de {0}"), FText::FromString(OwnerName));
}

void ACart::Interact(AErewhonPlayerController* Controller)
{
	if (Controller)
	{
		Controller->ShowToast(LOCTEXT("CartHint", "Placez-vous devant les brancards et appuyez sur F pour la conduire."));
	}
}

int32 ACart::FreeSlotNear(const FVector& World) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(World);
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const float Distance = FVector::Dist2D(Local, SlotTransform(Slot).GetLocation());
		if (!GetLoad(Slot) && Distance < BestDistance)
		{
			Best = Slot;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool ACart::IsAtHandles(const FVector& World) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(World);
	return Local.X < -70.f && Local.X > -320.f && FMath::Abs(Local.Y) < 140.f;
}

void ACart::SetLoad(int32 Slot, AStorageActor* Storage)
{
	if (Loads.IsValidIndex(Slot))
	{
		Loads[Slot] = Storage;
	}
}

void ACart::ApplyDriverCollision(APawn* Pawn, bool bIgnore)
{
	// Le conducteur ne doit pas buter contre sa charrette ni son chargement.
	if (!Pawn)
	{
		return;
	}
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, true, true);
	Attached.Add(this);
	for (AActor* Actor : Attached)
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

void ACart::OnRep_Driver()
{
	if (AppliedDriver.IsValid() && AppliedDriver.Get() != Driver)
	{
		ApplyDriverCollision(AppliedDriver.Get(), false);
	}
	AppliedDriver = Driver;
	ApplyDriverCollision(Driver, true);
}

void ACart::StartDriving(APawn* Pawn)
{
	// Le conducteur prend les brancards : la charrette le suivra (voir FollowDriver).
	Driver = Pawn;
	OnRep_Driver();
}

void ACart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Driver)
	{
		FollowDriver();
	}
	SpinWheels();
}

void ACart::SpinWheels()
{
	// Chaque roue roule sans glisser : elle tourne du chemin parcouru dans l'axe de la charrette.
	const FVector Forward = GetActorForwardVector();
	for (int32 i = 0; i < WheelPivots.Num(); ++i)
	{
		USceneComponent* Pivot = WheelPivots[i].Get();
		if (!Pivot)
		{
			continue;
		}
		const FVector Position = Pivot->GetComponentLocation();
		const float Travel = FVector::DotProduct(Position - LastWheelPositions[i], Forward);
		LastWheelPositions[i] = Position;
		if (FMath::Abs(Travel) < 0.01f || FMath::Abs(Travel) > 500.f)
		{
			continue;   // immobile, ou téléportée
		}
		WheelAngles[i] = FMath::Fmod(WheelAngles[i] - FMath::RadiansToDegrees(Travel / (WheelDiameter * 0.5f)), 360.f);
		Pivot->SetRelativeRotation(FRotator(WheelAngles[i], 0.f, 0.f));
	}
}

void ACart::PostNetReceiveLocationAndRotation()
{
	// Conduite : chaque machine la fait rouler elle-même derrière le conducteur, sans à-coups réseau.
	if (!Driver)
	{
		Super::PostNetReceiveLocationAndRotation();
	}
}

float ACart::GroundAt(const FVector& World) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonCartGround), false, this);
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	if (Driver)
	{
		Params.AddIgnoredActor(Driver);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, World + FVector(0, 0, 150), World - FVector(0, 0, 400), ECC_WorldStatic, Params))
	{
		return Hit.ImpactPoint.Z;
	}
	return World.Z;
}

void ACart::FollowDriver()
{
	// Comme une charrette à bras qu'on tire : le bout des brancards suit le joueur, et l'essieu
	// suit le bout des brancards à distance constante. La charrette pivote donc autour de ses
	// roues ; si l'on recule vers elle, elle est poussée et roule à l'envers.
	const float Length = AxleX - GripX;
	const FVector2D Grip(Driver->GetActorLocation());
	FVector2D Axle(GetActorTransform().TransformPosition(FVector(AxleX, 0.f, 0.f)));
	FVector2D Along = Axle - Grip;
	if (Along.SizeSquared() < 1.f)
	{
		return;
	}
	Along.Normalize();
	Axle = Grip + Along * Length;
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
	const FVector2D Origin = Axle - Along * AxleX;
	const float Ground = GroundAt(FVector(Axle, GetActorLocation().Z));
	SetActorLocationAndRotation(FVector(Origin, Ground), FRotator(0.f, Yaw, 0.f));
}

void ACart::StopDriving()
{
	APawn* Previous = Driver;
	Driver = nullptr;
	OnRep_Driver();
	if (Previous)
	{
		ApplyDriverCollision(Previous, false);
	}
	// Reposée sur le sol, à plat.
	FVector Location = GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonCartDrop), false, this);
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	if (Previous)
	{
		Params.AddIgnoredActor(Previous);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0, 0, 150), Location - FVector(0, 0, 400), ECC_WorldStatic, Params))
	{
		Location = Hit.ImpactPoint;
	}
	SetActorLocationAndRotation(Location, FRotator(0.f, GetActorRotation().Yaw, 0.f));
}

#undef LOCTEXT_NAMESPACE
