#include "Core/ErewhonGameMode.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonHUD.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Core/ErewhonWorldSave.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "Kismet/GameplayStatics.h"
#include "Player/ErewhonCharacter.h"
#include "TimerManager.h"
#include "World/SectorLayout.h"
#include "World/Door.h"
#include "World/Terrain.h"
#include "World/SpawnLayout.h"

#define LOCTEXT_NAMESPACE "ErewhonGameMode"

DEFINE_LOG_CATEGORY_STATIC(LogErewhonGame, Log, All);

AErewhonGameMode::AErewhonGameMode()
{
	GameStateClass = AErewhonGameState::StaticClass();
	PlayerStateClass = AErewhonPlayerState::StaticClass();
	PlayerControllerClass = AErewhonPlayerController::StaticClass();
	DefaultPawnClass = AErewhonCharacter::StaticClass();
	HUDClass = AErewhonHUD::StaticClass();
}

// ── Monde ────────────────────────────────────────────────────────────────────

void AErewhonGameMode::InitGameState()
{
	Super::InitGameState();

	const UErewhonSettings& Settings = UErewhonSettings::Get();
	SaveSlot = Settings.WorldSaveSlot;
#if !UE_BUILD_SHIPPING
	// -ErewhonSaveSlot=<nom> : les tests jouent sur leur propre monde, sans toucher au vôtre.
	FParse::Value(FCommandLine::Get(), TEXT("ErewhonSaveSlot="), SaveSlot);
#endif
	WorldSave = Cast<UErewhonWorldSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
	if (!WorldSave)
	{
		WorldSave = Cast<UErewhonWorldSave>(UGameplayStatics::CreateSaveGameObject(UErewhonWorldSave::StaticClass()));
		WorldSave->WorldSeed = FMath::Rand();
		// Un monde neuf commence au lever du Soleil, Usha encore couchée.
		WorldSave->WorldAge = Settings.SunPeriodSeconds * 0.03;
		UE_LOG(LogErewhonGame, Log, TEXT("Nouveau monde, seed %d"), WorldSave->WorldSeed);
	}

#if !UE_BUILD_SHIPPING
	// Pour tester une heure précise : -ErewhonWorldAge=<secondes>.
	FParse::Value(FCommandLine::Get(), TEXT("ErewhonWorldAge="), WorldSave->WorldAge);
#endif

	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	State->InitializeWorld(WorldSave->WorldSeed, WorldSave->WorldAge);
	FIntPoint Disowned(TNumericLimits<int32>::Max());
#if !UE_BUILD_SHIPPING
	FString Disown, DisownX, DisownY;
	if (FParse::Value(FCommandLine::Get(), TEXT("ErewhonDisown="), Disown, false) && Disown.Split(TEXT(","), &DisownX, &DisownY))
	{
		Disowned = FIntPoint(FCString::Atoi(*DisownX), FCString::Atoi(*DisownY));
	}
#endif
	for (const FSavedSectorOwner& Saved : WorldSave->SectorOwners)
	{
		if (Saved.Sector == Disowned)
		{
			UE_LOG(LogErewhonGame, Log, TEXT("Secteur %s rendu libre"), *ErewhonSector::ToString(Saved.Sector));
			continue;
		}
		// Un secteur devenu route (ou spawn) n'est plus à personne : son acheteur est remboursé.
		if (!ErewhonSector::IsPurchasable(Saved.Sector))
		{
			if (FSavedPlayer* Player = WorldSave->Players.Find(Saved.OwnerKey))
			{
				Player->Coins += Settings.SectorBuyPrice;
			}
			UE_LOG(LogErewhonGame, Log, TEXT("Secteur %s devenu non achetable : %s remboursé"), *ErewhonSector::ToString(Saved.Sector), *Saved.OwnerName);
			continue;
		}
		State->SetSectorOwner(Saved.Sector, Saved.OwnerKey, Saved.OwnerName, Saved.RoadLinks);
	}
	const double Now = State->GetWorldClock();
	for (const FSavedHarvest& Harvest : WorldSave->Harvests)
	{
		if (Now - Harvest.HarvestTime < Settings.RegrowSeconds)
		{
			State->AddHarvest(Harvest.Sector, Harvest.PlantIndex, Harvest.HarvestTime);
		}
	}
}

void AErewhonGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Les portes des cabanes sont des acteurs répliqués : leur état est partagé par tous.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const EShopKind Kind : ErewhonSpawn::ShopKinds)
	{
		const FTransform Hinge = ErewhonSpawn::DoorHingeTransform(Kind);
		GetWorld()->SpawnActor<ADoor>(Hinge.GetLocation(), Hinge.Rotator(), Params);
	}
	GetWorldTimerManager().SetTimer(AutosaveTimer, this, &AErewhonGameMode::SaveWorld,
		UErewhonSettings::Get().AutosaveIntervalSeconds, true);
}

void AErewhonGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SaveWorld();
	Super::EndPlay(EndPlayReason);
}

void AErewhonGameMode::SaveWorld()
{
	const AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!WorldSave || !State)
	{
		return;
	}

	WorldSave->WorldAge = State->GetWorldClock();
	WorldSave->SectorOwners.Reset();
	for (const FSectorOwnerEntry& Entry : State->GetOwnerEntries())
	{
		WorldSave->SectorOwners.Add({ Entry.Sector, Entry.OwnerKey, Entry.OwnerName, Entry.RoadLinks });
	}
	WorldSave->Harvests.Reset();
	for (const FHarvestEntry& Entry : State->GetHarvestEntries())
	{
		WorldSave->Harvests.Add({ Entry.Sector, Entry.PlantIndex, Entry.HarvestTime });
	}
	for (const APlayerState* PlayerState : State->PlayerArray)
	{
		CapturePlayer(Cast<AErewhonPlayerState>(PlayerState));
	}
	UGameplayStatics::SaveGameToSlot(WorldSave, SaveSlot, 0);
}

// ── Joueurs ──────────────────────────────────────────────────────────────────

void AErewhonGameMode::RestartPlayer(AController* NewPlayer)
{
	// Pas de PlayerStart dans la carte : le spawn est défini par le plan du secteur 0:0.
	if (NewPlayer && !NewPlayer->GetPawn())
	{
		RestartPlayerAtTransform(NewPlayer, ErewhonSpawn::PlayerSpawnTransform());
	}
}

void AErewhonGameMode::Logout(AController* Exiting)
{
	if (Exiting)
	{
		CapturePlayer(Exiting->GetPlayerState<AErewhonPlayerState>());
	}
	Super::Logout(Exiting);
}

void AErewhonGameMode::CapturePlayer(const AErewhonPlayerState* PlayerState)
{
	if (!WorldSave || !PlayerState || !PlayerState->IsRegistered())
	{
		return;
	}
	FSavedPlayer& Saved = WorldSave->Players.FindOrAdd(PlayerState->GetPlayerKey());
	Saved.Name = PlayerState->GetPlayerName();
	Saved.Coins = PlayerState->GetCoins();
	Saved.Slots = PlayerState->GetInventory()->GetSlots();
	if (const APawn* Pawn = PlayerState->GetPawn())
	{
		Saved.bHasLocation = true;
		Saved.Location = Pawn->GetActorLocation();
		Saved.Yaw = Pawn->GetControlRotation().Yaw;
	}
}

void AErewhonGameMode::RegisterPlayer(AErewhonPlayerController* Controller, const FString& RequestedKey, const FString& DisplayName)
{
	AErewhonPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (!PlayerState || PlayerState->IsRegistered())
	{
		return;
	}

	// Deux connexions ne partagent jamais une clé (ex. deux clients PIE sur un même poste).
	FString Key = RequestedKey.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::Digits) : RequestedKey.Left(64);
	const auto IsTaken = [this](const FString& Candidate)
	{
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			const AErewhonPlayerState* Typed = Cast<AErewhonPlayerState>(Other);
			if (Typed && Typed->GetPlayerKey() == Candidate)
			{
				return true;
			}
		}
		return false;
	};
	for (int32 Suffix = 2; IsTaken(Key); ++Suffix)
	{
		Key = FString::Printf(TEXT("%s#%d"), *RequestedKey.Left(60), Suffix);
	}

	PlayerState->SetPlayerKey(Key);
#if !UE_BUILD_SHIPPING
	// -ErewhonOwn=x,y : tests, le joueur reçoit ce secteur.
	FString Own;
	FString OwnX, OwnY;
	if (FParse::Value(FCommandLine::Get(), TEXT("ErewhonOwn="), Own, false) && Own.Split(TEXT(","), &OwnX, &OwnY))
	{
		const FIntPoint Sector(FCString::Atoi(*OwnX), FCString::Atoi(*OwnY));
		if (ErewhonSector::IsPurchasable(Sector))
		{
			GetGameState<AErewhonGameState>()->SetSectorOwner(Sector, Key, DisplayName);
		}
	}
#endif
	if (!DisplayName.IsEmpty())
	{
		PlayerState->SetPlayerName(DisplayName.Left(24));
	}

	UInventoryComponent* Inventory = PlayerState->GetInventory();
	if (const FSavedPlayer* Saved = WorldSave ? WorldSave->Players.Find(Key) : nullptr)
	{
		PlayerState->SetCoins(Saved->Coins);
		Inventory->RestoreSlots(Saved->Slots);
		if (APawn* Pawn = Controller->GetPawn(); Pawn && Saved->bHasLocation)
		{
			// Le relief a pu changer depuis la sauvegarde : on repose le joueur sur le sol.
			FVector Location = Saved->Location;
			Location.Z = ErewhonTerrain::Height(FVector2D(Location), WorldSave->WorldSeed) + 120.f;
			Pawn->TeleportTo(Location, FRotator(0, Saved->Yaw, 0));
			Controller->SetControlRotation(FRotator(0, Saved->Yaw, 0));
		}
		UE_LOG(LogErewhonGame, Log, TEXT("Retour de %s (%s)"), *PlayerState->GetPlayerName(), *Key);
	}
	else
	{
		PlayerState->SetCoins(UErewhonSettings::Get().StartingCoins);
		Inventory->ResetToDefault();
		UE_LOG(LogErewhonGame, Log, TEXT("Nouveau joueur %s (%s)"), *PlayerState->GetPlayerName(), *Key);
	}
}

// ── Récolte ──────────────────────────────────────────────────────────────────

FText AErewhonGameMode::TryHarvest(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 PlantIndex)
{
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
		return LOCTEXT("NotOwner", "Vous ne pouvez récolter que sur vos secteurs.");
	}

	USectorLayoutSubsystem* Layouts = GetWorld()->GetSubsystem<USectorLayoutSubsystem>();
	const TSharedRef<const FSectorLayout> Layout = Layouts->GetLayout(Sector, State->GetWorldSeed());
	if (!Layout->Plants.IsValidIndex(PlantIndex) || Layout->CoveredPlants.Contains(PlantIndex) || State->IsHarvested(Sector, PlantIndex))
	{
		return FText::GetEmpty();
	}

	const FPlantInstance& Plant = Layout->Plants[PlantIndex];
	// Tolérance pour la latence : le client a pu bouger depuis sa visée.
	const float MaxDistance = UErewhonSettings::Get().HarvestReach() + 250.f;
	if (FVector::Dist(Pawn->GetActorLocation(), Plant.Location) > MaxDistance)
	{
		return FText::GetEmpty();
	}

	UInventoryComponent* Inventory = PlayerState->GetInventory();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FName Hand = Inventory->GetHandItem();
	if (!Hand.IsNone())
	{
		const FItemDef* Tool = Items->Find(Hand);
		if (!Tool || !Tool->bHarvestTool)
		{
			return LOCTEXT("HandsBusy", "Les plantes se cueillent à main nue ou à la cerpe.");
		}
	}

	if (Inventory->StoreItem(Plant.ItemId, 1) != 1)
	{
		return LOCTEXT("InventoryFull", "Inventaire plein.");
	}
	State->AddHarvest(Sector, PlantIndex, State->GetWorldClock());
	return FText::GetEmpty();
}

// ── Commerce ─────────────────────────────────────────────────────────────────

bool AErewhonGameMode::IsNearMerchant(const AController* Controller, EShopKind Kind) const
{
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), ErewhonSpawn::MerchantLocation(Kind))
		<= UErewhonSettings::Get().MerchantReach() + 200.f;
}

FText AErewhonGameMode::TryBuySector(AErewhonPlayerController* Controller, const FIntPoint& Sector)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "Le gérant des secteurs est trop loin.");
	}
	if (!ErewhonSector::IsPurchasable(Sector)
		|| FMath::Max(FMath::Abs(Sector.X), FMath::Abs(Sector.Y)) > Settings.StoreSectorRadius)
	{
		return LOCTEXT("NotForSale", "Ce secteur n'est pas à vendre.");
	}
	if (State->FindSectorOwnerKey(Sector))
	{
		return LOCTEXT("AlreadyOwned", "Ce secteur a déjà un propriétaire.");
	}
	if (!PlayerState->SpendCoins(Settings.SectorBuyPrice))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	State->SetSectorOwner(Sector, PlayerState->GetPlayerKey(), PlayerState->GetPlayerName());
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryBuyRoadLink(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 Side)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "Le gérant des secteurs est trop loin.");
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey())
	{
		return LOCTEXT("NotYours", "Ce secteur ne vous appartient pas.");
	}
	if (Side < 0 || Side >= 4 || !ErewhonSector::IsRoad(Sector + ErewhonSector::Sides[Side]))
	{
		return LOCTEXT("NoRoad", "Il n'y a pas de route de ce côté.");
	}
	if (State->GetRoadLinks(Sector) & (1 << Side))
	{
		return LOCTEXT("AlreadyLinked", "Ce chemin existe déjà.");
	}
	if (!PlayerState->SpendCoins(UErewhonSettings::Get().RoadLinkPrice))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	State->AddRoadLink(Sector, Side);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TrySellSector(AErewhonPlayerController* Controller, const FIntPoint& Sector)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "Le gérant des secteurs est trop loin.");
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey())
	{
		return LOCTEXT("NotYours", "Ce secteur ne vous appartient pas.");
	}
	State->ClearSectorOwner(Sector);
	PlayerState->AddCoins(UErewhonSettings::Get().SectorSellPrice);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryBuyItem(AErewhonPlayerController* Controller, FName ItemId)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const FItemDef* Def = UItemDatabase::Get(this)->Find(ItemId);
	if (!PlayerState || !PlayerState->IsRegistered() || !Def || !IsNearMerchant(Controller, Def->ShopFor()))
	{
		return LOCTEXT("TooFar", "Le marchand est trop loin.");
	}
	if (!Def || Def->BuyPrice <= 0)
	{
		return LOCTEXT("NotSold", "Le marchand ne vend pas cet objet.");
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	if (Def->IsUnique() && Inventory->Owns(ItemId))
	{
		return FText::Format(LOCTEXT("AlreadyHave", "Vous possédez déjà : {0}."), Def->Name);
	}
	if (!Inventory->CanStore(ItemId, 1))
	{
		return LOCTEXT("InventoryFull", "Inventaire plein.");
	}
	if (!PlayerState->SpendCoins(Def->BuyPrice))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	Inventory->StoreItem(ItemId, 1);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TrySellItem(AErewhonPlayerController* Controller, FName ItemId, int32 Count)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const FItemDef* Def = UItemDatabase::Get(this)->Find(ItemId);
	if (!PlayerState || !PlayerState->IsRegistered() || !Def || !IsNearMerchant(Controller, Def->ShopFor()))
	{
		return LOCTEXT("TooFar", "Le marchand est trop loin.");
	}
	const int32 Price = Def ? Def->ResalePrice(UErewhonSettings::Get().ResaleRatio) : 0;
	if (Price <= 0)
	{
		return LOCTEXT("NotBought", "Le marchand n'achète pas cet objet.");
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	const int32 Sold = FMath::Min(Count, Inventory->CountStored(ItemId));
	if (Sold <= 0 || !Inventory->RemoveStored(ItemId, Sold))
	{
		return FText::GetEmpty();
	}
	PlayerState->AddCoins(Price * Sold);
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
