// Les champs du fermier : achat des champs à la mairie, travail de la terre, récolte, faux.

#include "Core/ErewhonGameMode.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "TimerManager.h"
#include "World/Apothecary.h"
#include "World/Farm.h"
#include "World/SectorLayout.h"
#include "World/Terrain.h"

#define LOCTEXT_NAMESPACE "ErewhonGameMode"

namespace
{
	/** La case tenue en main (la première case d'équipement de base). */
	FInventorySlot* HandSlot(UInventoryComponent* Inventory)
	{
		return Inventory->EditHandSlot();
	}
}

FText AErewhonGameMode::CheckFarmer(AErewhonPlayerController* Controller) const
{
	const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!PlayerState || !PlayerState->IsFarmer())
	{
		return LOCTEXT("FarmerOnly", "Réservé aux fermiers : choisissez cette classe à la mairie.");
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryBuyField(AErewhonPlayerController* Controller, const FIntPoint& Sector, uint8 Which)
{
	using namespace ErewhonFarm;
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey() || !ErewhonSector::IsPurchasable(Sector))
	{
		return LOCTEXT("NotYours", "Ce secteur ne vous appartient pas.");
	}
	if (const FText NotOne = CheckFarmer(Controller); !NotOne.IsEmpty())
	{
		return NotOne;
	}
	const ESectorType Type = ErewhonSector::TypeAt(Sector, State->GetWorldSeed());
	if (Type != ESectorType::Prairie && Type != ESectorType::Edge)
	{
		return LOCTEXT("FieldGround", "Les champs ne se font qu'en prairie ou en lisière.");
	}
	const uint8 Buildings = State->GetBuildings(Sector);
	if (Buildings & ESectorBuilding::Apothecary)
	{
		return LOCTEXT("FieldHouse", "L'apothicairerie occupe ce secteur : pas de place pour des champs.");
	}
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	uint8 Bit = 0;
	int32 Price = 0;
	if (Which == FenceWhich)
	{
		Bit = FenceBit;
		Price = Settings.FencePrice;
		if (Buildings & FenceBit)
		{
			return LOCTEXT("FenceOwned", "Le secteur est déjà clos.");
		}
		if (!(Buildings & AnyField))
		{
			return LOCTEXT("FenceNeedsField", "Il faut d'abord un champ sur ce secteur.");
		}
	}
	else if (Which < QuarterCount)
	{
		Bit = QuarterBit(Which);
		Price = Settings.FieldPrice;
		if (Buildings & (Bit | MergedBit))
		{
			return LOCTEXT("FieldOwned", "Ce champ est déjà à vous.");
		}
	}
	else
	{
		Bit = MergedBit;
		Price = Settings.FieldMergePrice;
		if (Buildings & MergedBit)
		{
			return LOCTEXT("FieldMerged", "Les champs sont déjà réunis.");
		}
		if ((Buildings & AllQuarters) != AllQuarters)
		{
			return LOCTEXT("FieldMergeNeedsAll", "Il faut d'abord les quatre champs du secteur.");
		}
	}
	if (!PlayerState->SpendCoins(Price))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	// La fusion recouvre la croix de chemins : ses cases deviennent des cases de champ (terre grossière).
	State->AddBuilding(Sector, Bit);
	Controller->ClientToast(Bit == FenceBit ? LOCTEXT("FenceBuilt", "Le secteur est clos : une barrière tout autour, un portique à chaque chemin.")
		: Bit == MergedBit ? LOCTEXT("FieldsMerged", "Les quatre champs n'en font plus qu'un.")
		: FText::Format(LOCTEXT("FieldBought", "Le champ {0} est à vous : de la terre grossière, à retourner à la bêche."), QuarterLabel(Which)));
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryWorkField(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 Cell, uint8 Corner, bool bNow)
{
	using namespace ErewhonFarm;
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !PlayerState->IsRegistered() || !Pawn)
	{
		return FText::GetEmpty();
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey())
	{
		return LOCTEXT("FieldNotYours", "Ce champ n'est pas à vous.");
	}
	const uint8 Buildings = State->GetBuildings(Sector);
	if (!IsFieldCell(Sector, Buildings, Cell)
		|| FVector::Dist2D(Pawn->GetActorLocation(), FVector(CellCenter(Sector, Cell), 0.f)) > UErewhonSettings::Get().HarvestReach() + 300.f)
	{
		return FText::GetEmpty();
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	const FName Hand = Inventory->GetHandItem();
	const double Now = State->GetWorldClock();
	TArray<int32> Area;

	// La bêche retourne la terre : les 2 × 2 cases autour du coin visé, en une seconde.
	if (Hand == Spade)
	{
		if (Controller->GetCraftJob().IsActive())
		{
			return FText::GetEmpty();   // un coup de bêche à la fois
		}
		const int32 X = Cell % CellsPerSide, Y = Cell / CellsPerSide;
		const int32 DX = (Corner & 1) ? 1 : -1, DY = (Corner & 2) ? 1 : -1;
		FPendingDig Dig;
		Dig.Sector = Sector;
		for (const FIntPoint Offset : { FIntPoint(0, 0), FIntPoint(DX, 0), FIntPoint(0, DY), FIntPoint(DX, DY) })
		{
			const int32 CX = X + Offset.X, CY = Y + Offset.Y;
			const int32 Around = CY * CellsPerSide + CX;
			if (CX >= 0 && CY >= 0 && CX < CellsPerSide && CY < CellsPerSide && IsFieldCell(Sector, Buildings, Around) && !State->FindFieldCell(Sector, Around))
			{
				Dig.Cells.Add(Around);
			}
		}
		if (Dig.Cells.IsEmpty())
		{
			return LOCTEXT("AlreadyTilled", "La terre est déjà retournée ici.");
		}
		PendingDigs.Add(Controller, Dig);
		FErewhonCraftJob Job;
		Job.Output = Spade;
		Job.Remaining = 1;
		Job.StepSeconds = SpadeSeconds;
		Job.StepEnd = Now + Job.StepSeconds;
		Job.Pour = 3;
		Controller->SetCraftJob(Job, nullptr);
		TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
		GetWorldTimerManager().SetTimer(Controller->CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
		{
			if (WeakController.IsValid())
			{
				FinishDig(WeakController.Get());
			}
		}), Job.StepSeconds, false);
		return FText::GetEmpty();
	}

	// Un sac de graines sème les cases retournées, trois sur trois.
	if (const int32 CropIndex = CropOfSeed(Hand); CropIndex != INDEX_NONE)
	{
		CellsAround(Cell, 1, Area);
		TArray<int32> Ready;
		for (const int32 Around : Area)
		{
			const FFieldCellEntry* Entry = State->FindFieldCell(Sector, Around);
			if (Entry && Entry->Crop == 0 && IsFieldCell(Sector, Buildings, Around))
			{
				Ready.Add(Around);
			}
		}
		if (Ready.IsEmpty())
		{
			return State->FindFieldCell(Sector, Cell) ? LOCTEXT("AlreadySown", "C'est déjà semé ici.")
				: LOCTEXT("TillFirst", "Retournez d'abord la terre, à la bêche (General Store).");
		}
		FInventorySlot* Slot = HandSlot(Inventory);
		if (!Slot || Slot->ItemId != Hand || Slot->Count <= 0)
		{
			return FText::GetEmpty();
		}
		for (const int32 Around : Ready)
		{
			State->SetFieldCell(Sector, Around, static_cast<uint8>(CropIndex + 1), Now);
		}
		// Un sac de moins ; le dernier vidé, on en reprend dans ses poches s'il en reste.
		if (--Slot->Count <= 0)
		{
			Slot->Clear();
			const int32 Stored = Inventory->CountStored(Hand);
			const FItemDef* Def = UItemDatabase::Get(this)->Find(Hand);
			const int32 Refill = FMath::Min(Stored, Def ? Def->MaxStack : 1);
			if (Refill > 0 && Inventory->RemoveStored(Hand, Refill))
			{
				Slot = HandSlot(Inventory);
				Slot->ItemId = Hand;
				Slot->Count = Refill;
			}
		}
		Inventory->CommitChanges();
		return FText::GetEmpty();
	}

	// À la main, une case ; à la cerpe, trois sur trois.
	const bool bSickle = Hand == Sickle;
	if (!Hand.IsNone() && !bSickle)
	{
		return LOCTEXT("FieldHandsBusy", "Bêche, sac de graines, cerpe ou mains nues : c'est selon.");
	}
	if (bSickle)
	{
		CellsAround(Cell, 1, Area);
	}
	else
	{
		Area.Add(Cell);
	}
	// On coupe d'abord (une seconde à la main, une demi à la cerpe), on ramasse au bout.
	if (!bNow)
	{
		const bool bAnyRipe = Area.ContainsByPredicate([&](int32 Around)
		{
			const FFieldCellEntry* Entry = State->FindFieldCell(Sector, Around);
			return Entry && Entry->Crop > 0 && Stage(Entry->SownAt, Now) >= RipeStage;
		});
		if (bAnyRipe)
		{
			StartTimedGesture(Controller, bSickle ? SickleHarvestSeconds : HandHarvestSeconds, bSickle ? 5 : 4,
				[this, Sector, Cell, Corner](AErewhonPlayerController* Picker) { return TryWorkField(Picker, Sector, Cell, Corner, true); });
			return FText::GetEmpty();
		}
	}
	int32 Ripe = 0;
	for (const int32 Around : Area)
	{
		const FFieldCellEntry* Entry = State->FindFieldCell(Sector, Around);
		if (!Entry || Entry->Crop == 0 || Stage(Entry->SownAt, Now) < RipeStage || !Crops().IsValidIndex(Entry->Crop - 1))
		{
			continue;
		}
		const FCrop& Crop = Crops()[Entry->Crop - 1];
		if (Inventory->StoreItem(Crop.Product, Crop.Yield) <= 0)
		{
			return LOCTEXT("InventoryFull", "Inventaire plein.");
		}
		// Récoltée, la case redevient de la terre grossière : on la retournera.
		State->ClearFieldCell(Sector, Around);
		++Ripe;
	}
	if (Ripe == 0)
	{
		const FFieldCellEntry* Entry = State->FindFieldCell(Sector, Cell);
		return Entry && Entry->Crop > 0 ? LOCTEXT("NotRipe", "Ce n'est pas encore mûr.") : FText::GetEmpty();
	}
	return FText::GetEmpty();
}

bool AErewhonGameMode::StartTimedGesture(AErewhonPlayerController* Controller, float Seconds, uint8 Pour, TFunction<FText(AErewhonPlayerController*)> Finish)
{
	const AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!State || Controller->GetCraftJob().IsActive())
	{
		return false;   // un geste à la fois
	}
	FErewhonCraftJob Job;
	Job.Output = TEXT("gesture");
	Job.Remaining = 1;
	Job.StepSeconds = Seconds;
	Job.StepEnd = State->GetWorldClock() + Seconds;
	Job.Pour = Pour;
	Controller->SetCraftJob(Job, nullptr);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	GetWorldTimerManager().SetTimer(Controller->CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakController, Finish = MoveTemp(Finish)]()
	{
		if (AErewhonPlayerController* Picker = WeakController.Get())
		{
			Picker->SetCraftJob(FErewhonCraftJob(), nullptr);
			ReportResultTo(Picker, Finish(Picker));
		}
	}), Seconds, false);
	return true;
}

void AErewhonGameMode::FinishDig(AErewhonPlayerController* Controller)
{
	FPendingDig Dig;
	const bool bFound = PendingDigs.RemoveAndCopyValue(Controller, Dig);
	Controller->SetCraftJob(FErewhonCraftJob(), nullptr);
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	const APawn* Pawn = Controller->GetPawn();
	// Toujours la bêche en main, toujours chez soi, toujours à portée.
	if (!bFound || !PlayerState || !Pawn || PlayerState->GetInventory()->GetHandItem() != ErewhonFarm::Spade)
	{
		return;
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Dig.Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey())
	{
		return;
	}
	const double Now = State->GetWorldClock();
	for (const int32 Cell : Dig.Cells)
	{
		if (FVector::Dist2D(Pawn->GetActorLocation(), FVector(ErewhonFarm::CellCenter(Dig.Sector, Cell), 0.f)) <= UErewhonSettings::Get().HarvestReach() + 300.f
			&& !State->FindFieldCell(Dig.Sector, Cell))
		{
			State->SetFieldCell(Dig.Sector, Cell, 0, Now);
		}
	}
}

FText AErewhonGameMode::TryScythe(AErewhonPlayerController* Controller)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !Controller->GetPawn() || PlayerState->GetInventory()->GetHandItem() != ErewhonFarm::Scythe)
	{
		return FText::GetEmpty();
	}
	if (const FText NotOne = CheckFarmer(Controller); !NotOne.IsEmpty())
	{
		return NotOne;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* Busy = ScytheUntil.Find(Controller); Busy && *Busy > Now)
	{
		return FText::GetEmpty();   // le geste est en cours
	}
	ScytheUntil.Add(Controller, Now + ErewhonFarm::ScytheSeconds);
	// Le geste dure deux secondes ; tout ce qui est à portée tombe à la fin.
	FTimerHandle Handle;
	TWeakObjectPtr<AErewhonPlayerController> Weak = Controller;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Weak]()
	{
		if (AErewhonPlayerController* Mower = Weak.Get())
		{
			ReportResultTo(Mower, FinishScythe(Mower));
		}
	}), ErewhonFarm::ScytheSeconds, false);
	return FText::GetEmpty();
}

void AErewhonGameMode::ReportResultTo(AErewhonPlayerController* Controller, const FText& Error)
{
	if (Controller && !Error.IsEmpty())
	{
		Controller->ClientToast(Error);
	}
}

FText AErewhonGameMode::FinishScythe(AErewhonPlayerController* Controller)
{
	using namespace ErewhonFarm;
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !Pawn || PlayerState->GetInventory()->GetHandItem() != Scythe)
	{
		return FText::GetEmpty();
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	const FString& Key = PlayerState->GetPlayerKey();
	const FVector Center = Pawn->GetActorLocation();
	const double Now = State->GetWorldClock();
	USectorLayoutSubsystem* Layouts = GetWorld()->GetSubsystem<USectorLayoutSubsystem>();
	int32 Taken = 0;
	bool bFull = false;
	const FIntPoint Here = ErewhonSector::FromWorld(Center);
	for (int32 DY = -1; DY <= 1 && !bFull; ++DY)
	{
		for (int32 DX = -1; DX <= 1 && !bFull; ++DX)
		{
			const FIntPoint Sector = Here + FIntPoint(DX, DY);
			const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
			if (!OwnerKey || *OwnerKey != Key)
			{
				continue;
			}
			// Les plantes sauvages à portée.
			const TSharedRef<const FSectorLayout> Layout = Layouts->GetLayout(Sector, State->GetWorldSeed());
			TArray<int32> Near;
			Layout->GatherNear(Center, ScytheRadius, Near);
			for (const int32 Index : Near)
			{
				if (Layout->CoveredPlants.Contains(Index) || State->IsHarvested(Sector, Index))
				{
					continue;
				}
				if (Inventory->StoreItem(Layout->Plants[Index].ItemId, 1) != 1)
				{
					bFull = true;
					break;
				}
				State->AddHarvest(Sector, Index, Now);
				++Taken;
			}
			// Les cultures mûres à portée.
			const TMap<int32, FFieldCellEntry>* Cells = State->GetFieldCells(Sector);
			if (!Cells || bFull)
			{
				continue;
			}
			TArray<int32> RipeCells;
			for (const TPair<int32, FFieldCellEntry>& Pair : *Cells)
			{
				if (Pair.Value.Crop > 0 && Stage(Pair.Value.SownAt, Now) >= RipeStage
					&& FVector2D::Distance(CellCenter(Sector, Pair.Key), FVector2D(Center)) <= ScytheRadius)
				{
					RipeCells.Add(Pair.Key);
				}
			}
			for (const int32 Cell : RipeCells)
			{
				const FFieldCellEntry* Entry = State->FindFieldCell(Sector, Cell);
				if (!Entry || !Crops().IsValidIndex(Entry->Crop - 1))
				{
					continue;
				}
				const FCrop& Crop = Crops()[Entry->Crop - 1];
				if (Inventory->StoreItem(Crop.Product, Crop.Yield) <= 0)
				{
					bFull = true;
					break;
				}
				State->ClearFieldCell(Sector, Cell);
				++Taken;
			}
		}
	}
	if (bFull)
	{
		return LOCTEXT("InventoryFull", "Inventaire plein.");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
