// Le bhurak : le monter, l'atteler, le harnacher.

#include "Core/ErewhonGameMode.h"

#include "Components/CapsuleComponent.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "Player/ErewhonCharacter.h"
#include "World/Bhurak.h"
#include "World/Storage.h"

#define LOCTEXT_NAMESPACE "ErewhonGameMode"

namespace
{
	constexpr float GBhurakReach = 500.f;

	bool NearBhurak(const APawn* Pawn, const ABhurak* Bhurak)
	{
		return Pawn && Bhurak && FVector::Dist2D(Pawn->GetActorLocation(), Bhurak->GetActorLocation()) <= Bhurak->GetHalfLength() + GBhurakReach;
	}
}

FText AErewhonGameMode::TryRideBhurak(AErewhonPlayerController* Controller, ABhurak* Bhurak)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonCharacter* Character = Controller->GetPawn<AErewhonCharacter>();
	if (!PlayerState || !Character)
	{
		return FText::GetEmpty();
	}
	// Descendre : à côté de sa monture, du côté gauche.
	if (ABhurak* Mount = Character->GetMount())
	{
		Mount->SetRider(nullptr);
		Character->SetMount(nullptr);
		const FVector Side = Mount->GetActorLocation() - Mount->GetActorRightVector() * (Mount->GetHalfWidth() + 70.f);
		Character->TeleportTo(Side + FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.f), Character->GetActorRotation());
		return FText::GetEmpty();
	}
	if (!Bhurak || !NearBhurak(Character, Bhurak))
	{
		return FText::GetEmpty();
	}
	if (!Bhurak->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("NotYourBhurak", "Ce bhurak n'est pas à vous.");
	}
	if (!Bhurak->HasSaddle())
	{
		return LOCTEXT("NeedSaddle", "Sans selle, on ne le monte pas : [E] pour le harnacher et le seller.");
	}
	if (Bhurak->GetRider())
	{
		return FText::GetEmpty();
	}
	if (Character->GetCarried() || Character->GetDrivenCart() || Character->IsSeated())
	{
		return LOCTEXT("HandsFullRide", "Posez d'abord ce que vous tenez.");
	}
	// En selle : le cavalier se place sur le bhurak, qui le suivra désormais.
	Character->TeleportTo(Bhurak->GetActorLocation() + FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f),
		Bhurak->GetActorRotation(), false, true);
	Controller->SetControlRotation(FRotator(0.f, Bhurak->GetActorRotation().Yaw, 0.f));
	Character->SetMount(Bhurak);
	Bhurak->SetRider(Character);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryHitchBhurak(AErewhonPlayerController* Controller, ABhurak* Aimed)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonCharacter* Character = Controller->GetPawn<AErewhonCharacter>();
	if (!PlayerState || !Character)
	{
		return FText::GetEmpty();
	}
	const FString& Key = PlayerState->GetPlayerKey();

	// On conduit une charrette jusqu'à son bhurak : on la lui attelle.
	if (ACart* Driven = Character->GetDrivenCart())
	{
		ABhurak* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<ABhurak> It(GetWorld()); It; ++It)
		{
			const float Distance = FVector::Dist2D(It->GetActorLocation(), Character->GetActorLocation());
			if (It->IsOwnedBy(Key) && Distance < BestDistance && NearBhurak(Character, *It))
			{
				Best = *It;
				BestDistance = Distance;
			}
		}
		if (!Best)
		{
			return LOCTEXT("NoBhurakNear", "Approchez la charrette de votre bhurak.");
		}
		if (!Best->HasHarness())
		{
			return LOCTEXT("NeedHarnessHitch", "Il lui faut un harnais pour tirer une charrette.");
		}
		if (Best->GetTowedCart())
		{
			return LOCTEXT("AlreadyTowing", "Il tire déjà une charrette.");
		}
		Driven->StopDriving();
		Character->SetDrivenCart(nullptr);
		Driven->StartDriving(Best);
		Best->SetTowedCart(Driven);
		return FText::GetEmpty();
	}

	// En selle, ou en visant son bhurak : dételer s'il tire déjà ; sinon atteler la charrette la plus proche.
	ABhurak* Bhurak = Character->GetMount() ? Character->GetMount() : Aimed;
	if (!Bhurak || !Bhurak->IsOwnedBy(Key) || !NearBhurak(Character, Bhurak))
	{
		return FText::GetEmpty();
	}
	if (ACart* Towed = Bhurak->GetTowedCart())
	{
		Towed->StopDriving();
		Bhurak->SetTowedCart(nullptr);
		return FText::GetEmpty();
	}
	if (!Character->GetMount())
	{
		return FText::GetEmpty();   // à pied, on attelle en amenant la charrette
	}
	if (!Bhurak->HasHarness())
	{
		return LOCTEXT("NeedHarnessHitch", "Il lui faut un harnais pour tirer une charrette.");
	}
	ACart* Best = nullptr;
	float BestDistance = 600.f;
	for (TActorIterator<ACart> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist2D(It->GetActorLocation(), Bhurak->GetHitchPoint());
		if (It->IsOwnedBy(Key) && !It->GetDriver() && Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	if (!Best)
	{
		return LOCTEXT("NoCartNear", "Aucune de vos charrettes n'est assez près.");
	}
	Best->StartDriving(Bhurak);
	Bhurak->SetTowedCart(Best);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryEquipBhurak(AErewhonPlayerController* Controller, ABhurak* Bhurak, uint8 Slot, FName Item)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !Bhurak || !NearBhurak(Pawn, Bhurak))
	{
		return LOCTEXT("BhurakTooFar", "Votre bhurak est trop loin.");
	}
	if (!Bhurak->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("NotYourBhurak", "Ce bhurak n'est pas à vous.");
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();

	// 4 : le reprendre dans son inventaire.
	if (Slot == 4)
	{
		if (Bhurak->HasHarness() || Bhurak->HasAnyGear() || Bhurak->GetRider() || Bhurak->GetTowedCart())
		{
			return LOCTEXT("StripFirst", "Retirez d'abord son harnachement.");
		}
		if (Inventory->StoreItem(ABhurak::CoatItem(Bhurak->GetCoat()), 1) != 1)
		{
			return LOCTEXT("InventoryFull", "Inventaire plein.");
		}
		Bhurak->Destroy();
		return FText::GetEmpty();
	}

	// 3 : le harnais.
	if (Slot == 3)
	{
		if (Item.IsNone())
		{
			if (!Bhurak->HasHarness() || Bhurak->HasAnyGear() || Bhurak->GetTowedCart())
			{
				return LOCTEXT("EmptyHarnessFirst", "Retirez d'abord sacoches et selle, et dételez la charrette.");
			}
			if (Inventory->StoreItem(ABhurak::HarnessItem, 1) != 1)
			{
				return LOCTEXT("InventoryFull", "Inventaire plein.");
			}
			Bhurak->SetHarness(false);
			return FText::GetEmpty();
		}
		if (Bhurak->HasHarness() || !Inventory->RemoveStored(ABhurak::HarnessItem, 1))
		{
			return FText::GetEmpty();
		}
		Bhurak->SetHarness(true);
		return FText::GetEmpty();
	}

	// 0-2 : une sacoche, ou la selle sur le dos.
	if (Slot >= ABhurak::GearSlots)
	{
		return FText::GetEmpty();
	}
	const FName Worn = Bhurak->GetGear(Slot);
	if (Item.IsNone())
	{
		if (Worn.IsNone())
		{
			return FText::GetEmpty();
		}
		AStorageActor* Bag = Bhurak->GetBag(Slot);
		if (Bag && Bag->GetSlots().ContainsByPredicate([](const FInventorySlot& S) { return !S.IsEmpty(); }))
		{
			return LOCTEXT("BagNotEmpty", "Videz d'abord la sacoche.");
		}
		if (Worn == ABhurak::SaddleItem && Bhurak->GetRider())
		{
			return LOCTEXT("Riding", "Descendez d'abord.");
		}
		if (Inventory->StoreItem(Worn, 1) != 1)
		{
			return LOCTEXT("InventoryFull", "Inventaire plein.");
		}
		Bhurak->SetGear(Slot, NAME_None, nullptr);
		if (Bag)
		{
			Bag->Destroy();
		}
		return FText::GetEmpty();
	}
	if (!Bhurak->HasHarness())
	{
		return LOCTEXT("NeedHarnessGear", "Il faut d'abord le harnais.");
	}
	if (!Worn.IsNone() || (Item != ABhurak::SaddlebagItem && Item != ABhurak::SaddleItem) || (Item == ABhurak::SaddleItem && Slot != ABhurak::BackSlot))
	{
		return LOCTEXT("SaddleOnBack", "La selle ne va que sur le dos.");
	}
	if (!Inventory->RemoveStored(Item, 1))
	{
		return FText::GetEmpty();
	}
	AStorageActor* Bag = nullptr;
	if (Item == ABhurak::SaddlebagItem)
	{
		Bag = SpawnStorage(EStorageKind::Saddlebag, Bhurak->GearTransform(Slot) * Bhurak->GetActorTransform(), Bhurak->GetOwnerKey(), Bhurak->GetOwnerName());
	}
	Bhurak->SetGear(Slot, Item, Bag);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryToggleImplement(AErewhonPlayerController* Controller, ACart* Implement)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !Pawn || !Implement || !Implement->IsImplement()
		|| FVector::Dist2D(Pawn->GetActorLocation(), Implement->GetActorLocation()) > 900.f)
	{
		return FText::GetEmpty();
	}
	if (!Implement->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("NotYourImplement", "Ce n'est pas à vous.");
	}
	Implement->SetEngaged(!Implement->IsEngaged());
	return FText::GetEmpty();
}

ABhurak* AErewhonGameMode::SpawnBhurak(const FTransform& At, const FString& OwnerKey, const FString& OwnerName, uint8 Coat)
{
	ABhurak* Bhurak = GetWorld()->SpawnActorDeferred<ABhurak>(ABhurak::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Bhurak->Setup(OwnerKey, OwnerName, Coat);
	Bhurak->FinishSpawning(At);
	return Bhurak;
}

#undef LOCTEXT_NAMESPACE
