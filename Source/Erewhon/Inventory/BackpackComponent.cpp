#include "Inventory/BackpackComponent.h"

#include "Core/ErewhonPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Items/ItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "World/Backpack.h"

UBackpackComponent::UBackpackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBackpackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBackpackComponent, bOwned);
	DOREPLIFETIME(UBackpackComponent, bWorn);
	DOREPLIFETIME(UBackpackComponent, Dropped);
	DOREPLIFETIME(UBackpackComponent, SideTools);
	DOREPLIFETIME_CONDITION(UBackpackComponent, Slots, COND_OwnerOnly);
}

void UBackpackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Le joueur part avec son sac : il réapparaîtra là où il l'a laissé à son retour.
	if (GetOwner() && GetOwner()->HasAuthority() && Dropped)
	{
		Dropped->Destroy();
		Dropped = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

const FString& UBackpackComponent::OwnerKey() const
{
	static const FString None;
	const AErewhonPlayerState* State = Cast<AErewhonPlayerState>(GetOwner());
	return State ? State->GetPlayerKey() : None;
}

float UBackpackComponent::GetTotalWeight() const
{
	const UItemDatabase* Items = UItemDatabase::Get(this);
	float Total = 0.f;
	for (const FInventorySlot& Slot : Slots)
	{
		if (const FItemDef* Def = Slot.IsEmpty() || !Items ? nullptr : Items->Find(Slot.ItemId))
		{
			Total += Def->GetWeight() * Slot.Count;
		}
	}
	return Total;
}

int32 UBackpackComponent::CountStored(FName ItemId) const
{
	int32 Total = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Kind == ESlotKind::Inventory && Slot.ItemId == ItemId)
		{
			Total += Slot.Count;
		}
	}
	return Total;
}

bool UBackpackComponent::IsAccessibleFrom(const APawn* Pawn, float Reach) const
{
	if (!bOwned || !Pawn)
	{
		return false;
	}
	// Sur le dos, on n'y a pas accès : il faut le poser pour y fouiller.
	return !bWorn && Dropped && FVector::Dist(Pawn->GetActorLocation(), Dropped->GetActorLocation()) <= Reach;
}

void UBackpackComponent::EnsureSlots()
{
	if (Slots.Num() == StorageSlots + ToolSlots)
	{
		return;
	}
	TArray<FInventorySlot> Previous = MoveTemp(Slots);
	Slots.SetNum(StorageSlots + ToolSlots);
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		Slots[i].Kind = i < StorageSlots ? ESlotKind::Inventory : ESlotKind::Equipment;
	}
	// Contenu d'une ancienne structure : on le range à plat.
	for (const FInventorySlot& Slot : Previous)
	{
		if (!Slot.IsEmpty())
		{
			StoreItem(Slot.ItemId, Slot.Count, true);
		}
	}
}

void UBackpackComponent::Grant()
{
	bOwned = true;
	bWorn = true;
	Slots.Reset();
	EnsureSlots();
	Commit();
}

void UBackpackComponent::Restore(bool bInOwned, bool bInWorn, const TArray<FInventorySlot>& Saved, const FTransform& DroppedAt)
{
	bOwned = bInOwned;
	if (!bOwned)
	{
		return;
	}
	const UItemDatabase* Items = UItemDatabase::Get(this);
	Slots = Saved;
	for (FInventorySlot& Slot : Slots)
	{
		if (!Slot.IsEmpty() && (!Items || !Items->Find(Slot.ItemId)))
		{
			Slot.Clear();   // objet retiré du catalogue
		}
	}
	EnsureSlots();
	bWorn = bInWorn;
	if (!bWorn)
	{
		Dropped = SpawnDropped(DroppedAt);
		bWorn = Dropped == nullptr;
	}
	Commit();
}

int32 UBackpackComponent::StoreItem(FName ItemId, int32 Count, bool bToolSlotsFirst)
{
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* Def = Items ? Items->Find(ItemId) : nullptr;
	if (!Def || Count <= 0 || !bOwned)
	{
		return 0;
	}
	EnsureSlots();
	int32 Remaining = Count;
	const auto Fill = [&](ESlotKind Kind)
	{
		// D'abord compléter les piles existantes, puis occuper les slots vides.
		for (const bool bEmptyPass : { false, true })
		{
			for (FInventorySlot& Slot : Slots)
			{
				if (Remaining == 0 || Slot.Kind != Kind || Slot.IsEmpty() != bEmptyPass || (!bEmptyPass && Slot.ItemId != ItemId))
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
	};
	if (bToolSlotsFirst && Def->Category == EItemCategory::Tool)
	{
		Fill(ESlotKind::Equipment);
	}
	Fill(ESlotKind::Inventory);
	if (Remaining != Count)
	{
		Commit();
	}
	return Count - Remaining;
}

bool UBackpackComponent::RemoveStored(FName ItemId, int32 Count)
{
	if (Count <= 0 || CountStored(ItemId) < Count)
	{
		return false;
	}
	int32 Remaining = Count;
	for (int32 i = Slots.Num() - 1; i >= 0 && Remaining > 0; --i)
	{
		FInventorySlot& Slot = Slots[i];
		if (Slot.Kind == ESlotKind::Inventory && Slot.ItemId == ItemId)
		{
			const int32 Taken = FMath::Min(Slot.Count, Remaining);
			Slot.Count -= Taken;
			Remaining -= Taken;
			if (Slot.Count <= 0)
			{
				Slot.Clear();
			}
		}
	}
	Commit();
	return true;
}

ABackpack* UBackpackComponent::SpawnDropped(const FTransform& Transform)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	ABackpack* Bag = World->SpawnActorDeferred<ABackpack>(ABackpack::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Bag)
	{
		const AErewhonPlayerState* State = Cast<AErewhonPlayerState>(GetOwner());
		Bag->SetOwnerInfo(OwnerKey(), State ? State->GetPlayerName() : FString());
		Bag->SetSideTools(SideTools);
		Bag->FinishSpawning(Transform);
	}
	return Bag;
}

bool UBackpackComponent::Drop(const APawn* Pawn)
{
	if (!IsWorn() || !Pawn)
	{
		return false;
	}
	// Devant soi, posé sur le sol (ou un plancher), les poches tournées vers le joueur.
	const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	FVector Location = Pawn->GetActorLocation() + Facing.Vector() * 80.f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonBackpackDrop), false, Pawn);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0, 0, 60), Location - FVector(0, 0, 400), ECC_WorldStatic, Params))
	{
		Location = Hit.ImpactPoint;
	}
	Dropped = SpawnDropped(FTransform(Facing + FRotator(0.f, 180.f, 0.f), Location));
	if (!Dropped)
	{
		return false;
	}
	bWorn = false;
	Commit();
	return true;
}

bool UBackpackComponent::PickUp()
{
	if (!bOwned || bWorn)
	{
		return false;
	}
	if (Dropped)
	{
		Dropped->Destroy();
		Dropped = nullptr;
	}
	bWorn = true;
	Commit();
	return true;
}

void UBackpackComponent::Commit()
{
	SideTools.Reset();
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Kind == ESlotKind::Equipment)
		{
			SideTools.Add(Slot.IsEmpty() ? NAME_None : Slot.ItemId);
		}
	}
	if (Dropped)
	{
		Dropped->SetSideTools(SideTools);
	}
	// Le serveur ne reçoit pas d'OnRep : on notifie directement (hôte d'une partie en écoute).
	OnBackpackChanged.Broadcast();
}

void UBackpackComponent::OnRep_State()
{
	OnBackpackChanged.Broadcast();
}
