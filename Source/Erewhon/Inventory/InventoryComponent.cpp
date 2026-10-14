#include "Inventory/InventoryComponent.h"

#include "Core/ErewhonSettings.h"
#include "Items/ItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerState.h"
#include "Player/ErewhonCharacter.h"

#define LOCTEXT_NAMESPACE "ErewhonInventory"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UInventoryComponent, Slots, COND_OwnerOnly);
	DOREPLIFETIME(UInventoryComponent, Appearance);
}

const FItemDef* UInventoryComponent::FindDef(FName ItemId) const
{
	const UItemDatabase* Items = UItemDatabase::Get(this);
	return Items ? Items->Find(ItemId) : nullptr;
}

// ── Lecture ──────────────────────────────────────────────────────────────────

int32 UInventoryComponent::CountStored(FName ItemId) const
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

bool UInventoryComponent::Owns(FName ItemId) const
{
	return Slots.ContainsByPredicate([ItemId](const FInventorySlot& Slot) { return !Slot.IsEmpty() && Slot.ItemId == ItemId; });
}

bool UInventoryComponent::CanStore(FName ItemId, int32 Count) const
{
	const FItemDef* Def = FindDef(ItemId);
	if (!Def || Count <= 0)
	{
		return false;
	}
	int32 Room = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Kind != ESlotKind::Inventory)
		{
			continue;
		}
		if (Slot.IsEmpty())
		{
			Room += Def->MaxStack;
		}
		else if (Slot.ItemId == ItemId)
		{
			Room += FMath::Max(0, Def->MaxStack - Slot.Count);
		}
		if (Room >= Count)
		{
			return true;
		}
	}
	return false;
}

int32 UInventoryComponent::FindFreeSlot(ESlotKind Kind, EClothingSlot ProvidedByToAvoid) const
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FInventorySlot& Slot = Slots[i];
		if (Slot.Kind == Kind && Slot.IsEmpty()
			&& (ProvidedByToAvoid == EClothingSlot::None || Slot.ProvidedBy != ProvidedByToAvoid))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UInventoryComponent::FindClothingSlot(EClothingSlot BodyPart) const
{
	return Slots.IndexOfByPredicate([BodyPart](const FInventorySlot& Slot) { return Slot.Kind == ESlotKind::Clothing && Slot.BodyPart == BodyPart; });
}

bool UInventoryComponent::ArePocketsEmpty(EClothingSlot BodyPart) const
{
	return !Slots.ContainsByPredicate([BodyPart](const FInventorySlot& Slot) { return Slot.ProvidedBy == BodyPart && !Slot.IsEmpty(); });
}

// ── Structure ────────────────────────────────────────────────────────────────

void UInventoryComponent::ResetToDefault()
{
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	Slots.Reset();
	for (int32 i = 0; i < Settings.BaseInventorySlots; ++i)
	{
		Slots.AddDefaulted_GetRef().Kind = ESlotKind::Inventory;
	}
	for (int32 i = 0; i < Settings.BaseEquipmentSlots; ++i)
	{
		Slots.AddDefaulted_GetRef().Kind = ESlotKind::Equipment;
	}
	for (const EClothingSlot Part : { EClothingSlot::Torso, EClothingSlot::Legs })
	{
		FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Kind = ESlotKind::Clothing;
		Slot.BodyPart = Part;
	}
	Commit();
}

bool UInventoryComponent::IsStructureValid(const TArray<FInventorySlot>& Candidate) const
{
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	int32 BaseInventory = 0;
	int32 BaseEquipment = 0;
	TMap<EClothingSlot, int32> Worn;
	TMap<EClothingSlot, int32> Pockets;

	for (const FInventorySlot& Slot : Candidate)
	{
		if (!Slot.IsEmpty() && !FindDef(Slot.ItemId))
		{
			return false;
		}
		if (Slot.ProvidedBy != EClothingSlot::None)
		{
			++Pockets.FindOrAdd(Slot.ProvidedBy);
		}
		else if (Slot.Kind == ESlotKind::Inventory)
		{
			++BaseInventory;
		}
		else if (Slot.Kind == ESlotKind::Equipment)
		{
			++BaseEquipment;
		}
		else
		{
			Worn.FindOrAdd(Slot.BodyPart) += Slot.IsEmpty() ? 0 : 1;
			if (!Slot.IsEmpty() && FindDef(Slot.ItemId)->ClothingSlot != Slot.BodyPart)
			{
				return false;
			}
		}
	}
	if (BaseInventory != Settings.BaseInventorySlots || BaseEquipment != Settings.BaseEquipmentSlots)
	{
		return false;
	}
	for (const EClothingSlot Part : { EClothingSlot::Torso, EClothingSlot::Legs })
	{
		const int32 SlotIndex = Candidate.IndexOfByPredicate([Part](const FInventorySlot& S) { return S.Kind == ESlotKind::Clothing && S.BodyPart == Part; });
		if (SlotIndex == INDEX_NONE)
		{
			return false;
		}
		const FItemDef* Wearing = Candidate[SlotIndex].IsEmpty() ? nullptr : FindDef(Candidate[SlotIndex].ItemId);
		const int32 Expected = Wearing ? Wearing->PocketSlots + Wearing->ExtraEquipSlots : 0;
		if (Pockets.FindRef(Part) != Expected)
		{
			return false;
		}
	}
	return true;
}

void UInventoryComponent::RestoreSlots(const TArray<FInventorySlot>& Saved)
{
	if (IsStructureValid(Saved))
	{
		Slots = Saved;
		Commit();
		return;
	}

	// Réglages ou catalogue modifiés depuis la sauvegarde : on range tout à plat.
	ResetToDefault();
	for (const FInventorySlot& Slot : Saved)
	{
		if (!Slot.IsEmpty() && FindDef(Slot.ItemId))
		{
			StoreItem(Slot.ItemId, Slot.Count);
		}
	}
}

void UInventoryComponent::AddPockets(EClothingSlot BodyPart, const FItemDef& Clothing)
{
	for (int32 i = 0; i < Clothing.PocketSlots; ++i)
	{
		FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Kind = ESlotKind::Inventory;
		Slot.ProvidedBy = BodyPart;
	}
	for (int32 i = 0; i < Clothing.ExtraEquipSlots; ++i)
	{
		FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Kind = ESlotKind::Equipment;
		Slot.ProvidedBy = BodyPart;
	}
}

void UInventoryComponent::RemovePockets(EClothingSlot BodyPart)
{
	Slots.RemoveAll([BodyPart](const FInventorySlot& Slot) { return Slot.ProvidedBy == BodyPart; });
}

// ── Contenu ──────────────────────────────────────────────────────────────────

int32 UInventoryComponent::StoreItem(FName ItemId, int32 Count)
{
	const FItemDef* Def = FindDef(ItemId);
	if (!Def || Count <= 0)
	{
		return 0;
	}

	int32 Remaining = Count;
	// D'abord compléter les piles existantes, puis occuper les slots vides.
	for (const bool bEmptyPass : { false, true })
	{
		for (FInventorySlot& Slot : Slots)
		{
			if (Remaining == 0)
			{
				break;
			}
			if (Slot.Kind != ESlotKind::Inventory || Slot.IsEmpty() != bEmptyPass)
			{
				continue;
			}
			if (!bEmptyPass && Slot.ItemId != ItemId)
			{
				continue;
			}
			const int32 Room = Def->MaxStack - (bEmptyPass ? 0 : Slot.Count);
			const int32 Moved = FMath::Min(Room, Remaining);
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

bool UInventoryComponent::RemoveStored(FName ItemId, int32 Count)
{
	if (Count <= 0 || CountStored(ItemId) < Count)
	{
		return false;
	}
	int32 Remaining = Count;
	// On vide les poches avant les slots de base.
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

// ── Équipement ───────────────────────────────────────────────────────────────

FText UInventoryComponent::UseSlot(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return FText::GetEmpty();
	}
	const FItemDef* Def = FindDef(Slots[SlotIndex].ItemId);
	if (!Def)
	{
		return FText::GetEmpty();
	}
	if (Slots[SlotIndex].Kind == ESlotKind::Inventory && Def->IsPotion())
	{
		// Une potion se boit : un exemplaire en moins, son effet pour quelques minutes.
		const APlayerState* State = Cast<APlayerState>(GetOwner());
		AErewhonCharacter* Character = State ? Cast<AErewhonCharacter>(State->GetPawn()) : nullptr;
		if (!Character)
		{
			return FText::GetEmpty();
		}
		Character->DrinkPotion(Def->PotionEffect);
		if (--Slots[SlotIndex].Count <= 0)
		{
			Slots[SlotIndex].Clear();
		}
		StoreItem(TEXT("vial"), 1);   // il reste la fiole vide (si l'on a de la place)
		Commit();
		return FText::GetEmpty();
	}
	if (Slots[SlotIndex].Kind == ESlotKind::Inventory)
	{
		return (Def->IsEquipable() || Def->IsClothing()) ? EquipFrom(SlotIndex, *Def) : FText::GetEmpty();
	}
	return UnequipFrom(SlotIndex, *Def);
}

FText UInventoryComponent::EquipFrom(int32 SlotIndex, const FItemDef& Def)
{
	if (Def.IsEquipable())
	{
		int32 Target = FindFreeSlot(ESlotKind::Equipment);
		if (Target == INDEX_NONE)
		{
			// Tout est occupé : l'objet prend la place de celui qu'on tient en main.
			Target = Slots.IndexOfByPredicate([](const FInventorySlot& S) { return S.Kind == ESlotKind::Equipment; });
		}
		if (Target == INDEX_NONE)
		{
			return LOCTEXT("NoEquipSlot", "Aucun slot d'équipement.");
		}
		Swap(Slots[SlotIndex].ItemId, Slots[Target].ItemId);
		Swap(Slots[SlotIndex].Count, Slots[Target].Count);
		Commit();
		return FText::GetEmpty();
	}

	const EClothingSlot Part = Def.ClothingSlot;
	const int32 Target = FindClothingSlot(Part);
	if (Target == INDEX_NONE)
	{
		return LOCTEXT("NoClothingSlot", "Ce vêtement ne se porte pas.");
	}
	if (!Slots[Target].IsEmpty())
	{
		const FItemDef* Worn = FindDef(Slots[Target].ItemId);
		if (!ArePocketsEmpty(Part))
		{
			return FText::Format(LOCTEXT("PocketsNotEmpty", "Videz d'abord les poches : {0}."), Worn ? Worn->Name : FText::GetEmpty());
		}
	}

	// Échange : l'ancien vêtement (ou rien) rejoint le slot d'origine.
	Swap(Slots[SlotIndex].ItemId, Slots[Target].ItemId);
	Swap(Slots[SlotIndex].Count, Slots[Target].Count);
	RemovePockets(Part);
	AddPockets(Part, Def);
	Commit();
	return FText::GetEmpty();
}

FText UInventoryComponent::UnequipFrom(int32 SlotIndex, const FItemDef& Def)
{
	const FInventorySlot& Source = Slots[SlotIndex];
	const bool bClothing = Source.Kind == ESlotKind::Clothing;
	const EClothingSlot Part = Source.BodyPart;

	if (bClothing && !ArePocketsEmpty(Part))
	{
		return FText::Format(LOCTEXT("EmptyBeforeRemove", "Videz d'abord les poches : {0}."), Def.Name);
	}

	// Un vêtement ne peut pas se ranger dans ses propres poches.
	const int32 Target = FindFreeSlot(ESlotKind::Inventory, bClothing ? Part : EClothingSlot::None);
	if (Target == INDEX_NONE)
	{
		return LOCTEXT("InventoryFull", "Inventaire plein.");
	}

	Swap(Slots[SlotIndex].ItemId, Slots[Target].ItemId);
	Swap(Slots[SlotIndex].Count, Slots[Target].Count);
	if (bClothing)
	{
		RemovePockets(Part);
	}
	Commit();
	return FText::GetEmpty();
}

void UInventoryComponent::ServerUseSlot_Implementation(int32 SlotIndex)
{
	const FText Error = UseSlot(SlotIndex);
	if (!Error.IsEmpty())
	{
		ClientInventoryMessage(Error);
	}
}

void UInventoryComponent::ClientInventoryMessage_Implementation(const FText& Message)
{
	OnInventoryMessage.Broadcast(Message);
}

// ── Réplication ──────────────────────────────────────────────────────────────

void UInventoryComponent::Commit()
{
	FInventoryAppearance NewAppearance;
	bool bHandFound = false;
	for (const FInventorySlot& Slot : Slots)
	{
		// Le premier slot d'équipement de base est la main.
		if (Slot.Kind == ESlotKind::Equipment && Slot.ProvidedBy == EClothingSlot::None && !bHandFound)
		{
			bHandFound = true;
			NewAppearance.HandItem = Slot.IsEmpty() ? NAME_None : Slot.ItemId;
		}
		else if (Slot.Kind == ESlotKind::Clothing && !Slot.IsEmpty())
		{
			(Slot.BodyPart == EClothingSlot::Torso ? NewAppearance.TorsoItem : NewAppearance.LegsItem) = Slot.ItemId;
		}
	}
	Appearance = NewAppearance;

	// Le serveur ne reçoit pas d'OnRep : on notifie directement (hôte d'une partie en écoute).
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::OnRep_Slots()
{
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::OnRep_Appearance()
{
	OnInventoryChanged.Broadcast();
}

#undef LOCTEXT_NAMESPACE
