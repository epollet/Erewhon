#include "Core/ErewhonGameMode.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Inventory/InventoryComponent.h"
#include "TimerManager.h"
#include "World/DroppedItem.h"
#include "World/FelledTree.h"
#include "World/Forestry.h"
#include "World/SectorLayout.h"
#include "World/SectorTypes.h"
#include "World/Storage.h"
#include "World/Terrain.h"

#define LOCTEXT_NAMESPACE "ErewhonForestry"

// ── Forestier ────────────────────────────────────────────────────────────────

FText AErewhonGameMode::CheckForester(AErewhonPlayerController* Controller) const
{
	const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!PlayerState || !PlayerState->IsForester())
	{
		return LOCTEXT("ForesterOnly", "Réservé aux forestiers : choisissez cette classe à la mairie.");
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryChopTree(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 TreeIndex)
{
	using namespace ErewhonForestry;
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !PlayerState->IsRegistered() || !Pawn || !State || PlayerState->GetInventory()->GetHandItem() != Axe)
	{
		return FText::GetEmpty();
	}
	if (const FText NotOne = CheckForester(Controller); !NotOne.IsEmpty())
	{
		return NotOne;
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey())
	{
		return LOCTEXT("TreeNotYours", "On n'abat que les arbres de ses secteurs.");
	}
	const TSharedRef<const FSectorLayout> Layout = GetWorld()->GetSubsystem<USectorLayoutSubsystem>()->GetLayout(Sector, State->GetWorldSeed());
	if (!Layout->Trees.IsValidIndex(TreeIndex) || State->IsHarvested(Sector, HarvestIndexOfTree(TreeIndex)))
	{
		return FText::GetEmpty();
	}
	const FTreeInstance& Tree = Layout->Trees[TreeIndex];
	if (FVector::Dist2D(Pawn->GetActorLocation(), Tree.Location) > UErewhonSettings::Get().HarvestReach() + 250.f)
	{
		return FText::GetEmpty();
	}
	// Un coup à la fois, au rythme du geste.
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* Last = LastAxeHit.Find(Controller); Last && Now - *Last < HitInterval)
	{
		return FText::GetEmpty();
	}
	LastAxeHit.Add(Controller, Now);
	const TPair<FIntPoint, int32> Key(Sector, TreeIndex);
	int32& Hits = TreeHits.FindOrAdd(Key);
	if (++Hits < HitsToFell(Tree))
	{
		return FText::GetEmpty();
	}
	TreeHits.Remove(Key);

	// Il tombe à l'opposé du bûcheron, et il reste sa souche.
	FVector Away = Tree.Location - Pawn->GetActorLocation();
	Away.Z = 0.f;
	const float FallYaw = Away.IsNearlyZero() ? Tree.Yaw : Away.Rotation().Yaw;
	State->AddHarvest(Sector, HarvestIndexOfTree(TreeIndex), State->GetWorldClock());
	const FTransform At(FRotator::ZeroRotator, FVector(Tree.Location.X, Tree.Location.Y, ErewhonTerrain::Height(FVector2D(Tree.Location), State->GetWorldSeed())));
	AFelledTree* Felled = GetWorld()->SpawnActorDeferred<AFelledTree>(AFelledTree::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Felled->Setup(Tree, FallYaw, State->GetWorldClock(), PlayerState->GetPlayerKey(), PlayerState->GetPlayerName());
	Felled->FinishSpawning(At);
	// Au sol, la cime éclate : des branches feuillues tout autour.
	TWeakObjectPtr<AFelledTree> WeakFelled = Felled;
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, WeakFelled]()
	{
		if (WeakFelled.IsValid())
		{
			SpawnBranches(WeakFelled.Get());
		}
	}), FallSeconds, false);
	return FText::GetEmpty();
}

void AErewhonGameMode::SpawnBranches(AFelledTree* Felled)
{
	const AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!State)
	{
		return;
	}
	FRandomStream Rand(Felled->GetUniqueID());
	const int32 Count = FMath::Clamp(FMath::RoundToInt32(Felled->GetTreeHeight() * Felled->GetGirth() / 300.f), 3, 7);
	const FVector Along = FRotator(0.f, Felled->GetFallYaw(), 0.f).Vector();
	const FVector Side(-Along.Y, Along.X, 0.f);
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Spot = Felled->CrownCenter() + Along * Rand.FRandRange(-0.6f, 0.6f) * Felled->CrownRadius()
			+ Side * Rand.FRandRange(-1.f, 1.f) * Felled->CrownRadius();
		DropAt(Spot, ErewhonForestry::Branch, Rand.RandRange(1, 2), ErewhonForestry::BranchSeconds, false);
	}
}

FText AErewhonGameMode::TryChopLog(AErewhonPlayerController* Controller, AFelledTree* Felled, bool bNow)
{
	using namespace ErewhonForestry;
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !Pawn || !Felled || !Felled->HasLanded() || Felled->GetLogsLeft() <= 0)
	{
		return FText::GetEmpty();
	}
	if (const FText NotOne = CheckForester(Controller); !NotOne.IsEmpty())
	{
		return NotOne;
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	if (Inventory->GetHandItem() != Axe)
	{
		return LOCTEXT("NeedAxe", "La hache en main pour débiter le tronc.");
	}
	if (!Felled->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("TrunkNotYours", "Ce tronc n'est pas à vous.");
	}
	if (FVector::Dist2D(Pawn->GetActorLocation(), Felled->GetActorLocation()) > ErewhonForestry::TrunkLength(FTreeInstance{ FVector::ZeroVector, Felled->GetTreeHeight(), 0.f, 0, Felled->GetGirth() }) + 500.f)
	{
		return FText::GetEmpty();
	}
	if (RoomForLogs(Inventory) <= 0)
	{
		return LOCTEXT("ArmsFull", "Vos bras sont pleins : on ne porte que deux bûches. Posez-les en tas, ou dans la charrette.");
	}
	if (!bNow)
	{
		TWeakObjectPtr<AFelledTree> WeakFelled = Felled;
		StartTimedGesture(Controller, ChopLogSeconds, 7, [this, WeakFelled](AErewhonPlayerController* Chopper)
		{
			return WeakFelled.IsValid() ? TryChopLog(Chopper, WeakFelled.Get(), true) : FText::GetEmpty();
		});
		return FText::GetEmpty();
	}
	if (Inventory->StoreItem(Log, 1) != 1)
	{
		return LOCTEXT("InventoryFull", "Inventaire plein.");
	}
	Inventory->CommitChanges();
	Felled->SetLogsLeft(Felled->GetLogsLeft() - 1);
	if (Felled->GetLogsLeft() <= 0)
	{
		Felled->Destroy();
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryTakeLogs(AErewhonPlayerController* Controller, AStorageActor* Pile)
{
	using namespace ErewhonForestry;
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !Pawn || !Pile || Pile->GetKind() != EStorageKind::LogPile || FVector::Dist(Pawn->GetActorLocation(), Pile->GetActorLocation()) > 500.f)
	{
		return FText::GetEmpty();
	}
	if (!Pile->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("PileNotYours", "Ce tas de bûches n'est pas à vous.");
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	const int32 Wanted = FMath::Min(RoomForLogs(Inventory), Pile->CountStored(Log));
	if (Wanted <= 0)
	{
		return Pile->CountStored(Log) > 0 ? LOCTEXT("ArmsFullPile", "Vos bras sont pleins : on ne porte que deux bûches.") : FText::GetEmpty();
	}
	// D'abord dans la main (on les reposera d'un clic), sinon dans les poches.
	int32 Taken = 0;
	if (FInventorySlot* Hand = Inventory->EditHandSlot(); Hand && (Hand->IsEmpty() || Hand->ItemId == Log))
	{
		const int32 Put = FMath::Min(Wanted, LogCarry - (Hand->IsEmpty() ? 0 : Hand->Count));
		if (Put > 0)
		{
			Hand->ItemId = Log;
			Hand->Count = (Hand->IsEmpty() ? 0 : Hand->Count) + Put;
			Taken = Put;
		}
	}
	if (Taken < Wanted)
	{
		Taken += Inventory->StoreItem(Log, Wanted - Taken);
	}
	if (Taken <= 0)
	{
		return LOCTEXT("InventoryFull", "Inventaire plein.");
	}
	// Retirées du haut du tas.
	int32 Left = Taken;
	for (int32 i = Pile->EditSlots().Num() - 1; i >= 0 && Left > 0; --i)
	{
		FInventorySlot& Slot = Pile->EditSlots()[i];
		if (Slot.ItemId == Log)
		{
			const int32 Out = FMath::Min(Left, Slot.Count);
			Slot.Count -= Out;
			Left -= Out;
			if (Slot.Count <= 0)
			{
				Slot.Clear();
			}
		}
	}
	Inventory->CommitChanges();
	if (Pile->CountStored(Log) <= 0)
	{
		Pile->Destroy();   // un tas vide n'est plus un tas
	}
	else
	{
		Pile->Commit();
	}
	return FText::GetEmpty();
}

void AErewhonGameMode::DropAt(const FVector& Where, FName ItemId, int32 Count, double LifeSeconds, bool bMerge)
{
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (ItemId.IsNone() || Count <= 0 || !State)
	{
		return;
	}
	// Le sol (un plancher, une dalle) sous ce point ; à défaut, le relief du terrain.
	FVector Spot = Where;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonDropAt), false);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Spot + FVector(0, 0, 300.f), Spot - FVector(0, 0, 600.f), ECC_WorldStatic, Params))
	{
		Spot = Hit.ImpactPoint;
	}
	else
	{
		Spot.Z = ErewhonTerrain::Height(FVector2D(Spot), State->GetWorldSeed());
	}
	const FIntPoint Sector = ErewhonSector::FromWorld(Spot);
	const FString* LandOwner = ErewhonSector::IsPurchasable(Sector) ? State->FindSectorOwnerKey(Sector) : nullptr;
	const FString OwnerKey = LandOwner ? *LandOwner : FString();
	const double ExpiresAt = State->GetWorldClock() + LifeSeconds;
	if (bMerge)
	{
		// Le même objet au même endroit : il rejoint le tas.
		for (TActorIterator<ADroppedItem> It(GetWorld()); It; ++It)
		{
			if (It->GetItemId() == ItemId && It->CanPickUp(OwnerKey) && FVector::Dist2D(It->GetActorLocation(), Spot) < 120.f)
			{
				It->AddCount(Count, ExpiresAt);
				return;
			}
		}
	}
	const FTransform At(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Spot + FVector(0.f, 0.f, 16.f));
	ADroppedItem* Dropped = GetWorld()->SpawnActorDeferred<ADroppedItem>(ADroppedItem::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Dropped->Setup(ItemId, Count, OwnerKey, LandOwner ? State->GetSectorOwnerName(Sector) : FString(), ExpiresAt);
	Dropped->FinishSpawning(At);
}

#undef LOCTEXT_NAMESPACE
