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
#include "Inventory/BackpackComponent.h"
#include "World/Backpack.h"
#include "World/Village.h"
#include "World/Storage.h"
#include "Player/ErewhonCharacter.h"
#include "EngineUtils.h"
#include "World/SpawnLayout.h"
#include "World/Apothecary.h"

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
	int32 Village = WorldSave->VillageUpgrades;
#if !UE_BUILD_SHIPPING
	// -ErewhonVillage=<masque> : tests, le village avec ces améliorations (-1 : toutes).
	FParse::Value(FCommandLine::Get(), TEXT("ErewhonVillage="), Village);
#endif
	State->SetVillageUpgrades(Village);
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
		State->SetSectorOwner(Saved.Sector, Saved.OwnerKey, Saved.OwnerName, Saved.RoadLinks, Saved.PavedLinks, Saved.Buildings);
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
	SpawnDoors();
	SpawnSavedObjects();
	// Les apothicaireries construites, avec leur feu tel qu'on l'a laissé.
	for (const FSectorOwnerEntry& Entry : GetGameState<AErewhonGameState>()->GetOwnerEntries())
	{
		if (Entry.Buildings & ESectorBuilding::Apothecary)
		{
			const FSavedSectorOwner* Saved = WorldSave->SectorOwners.FindByPredicate([&](const FSavedSectorOwner& S) { return S.Sector == Entry.Sector; });
			SpawnApothecary(Entry.Sector, Saved ? Saved->FireUntil : 0.0, Saved ? Saved->Shelves : TArray<FInventorySlot>());
		}
	}
	GetGameState<AErewhonGameState>()->OnVillageChanged.AddUObject(this, &AErewhonGameMode::SpawnDoors);
	GetWorldTimerManager().SetTimer(AutosaveTimer, this, &AErewhonGameMode::SaveWorld,
		UErewhonSettings::Get().AutosaveIntervalSeconds, true);
}

void AErewhonGameMode::SpawnDoors()
{
	const AErewhonGameState* State = GetGameState<AErewhonGameState>();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const EShopKind Kind : ErewhonSpawn::ShopKinds)
	{
		if (DoorsSpawned.Contains(Kind) || (Kind == EShopKind::Tailor && !State->HasVillageUpgrade(EVillageUpgrade::Tailor)))
		{
			continue;
		}
		DoorsSpawned.Add(Kind);
		const FTransform Hinge = ErewhonSpawn::DoorHingeTransform(Kind);
		GetWorld()->SpawnActor<ADoor>(Hinge.GetLocation(), Hinge.Rotator(), Params);
	}
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
	WorldSave->VillageUpgrades = GetGameState<AErewhonGameState>()->GetVillageUpgrades();
	CaptureObjects();
	WorldSave->SectorOwners.Reset();
	for (const FSectorOwnerEntry& Entry : State->GetOwnerEntries())
	{
		const TObjectPtr<AApothecary>* House = Apothecaries.Find(Entry.Sector);
		WorldSave->SectorOwners.Add({ Entry.Sector, Entry.OwnerKey, Entry.OwnerName, Entry.RoadLinks, Entry.PavedLinks, Entry.Buildings,
			House && *House ? (*House)->GetFireUntil() : 0.0, House && *House ? (*House)->GetShelves() : TArray<FInventorySlot>() });
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
		ReleaseHeld(Exiting->GetPawn());
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
	const UBackpackComponent* Bag = PlayerState->GetBackpack();
	Saved.bHasBackpack = Bag->IsOwned();
	Saved.bBackpackWorn = Bag->IsWorn() || !Bag->GetDropped();
	Saved.BackpackSlots = Bag->GetSlots();
	if (const ABackpack* Dropped = Bag->GetDropped())
	{
		Saved.BackpackTransform = Dropped->GetActorTransform();
	}
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
		PlayerState->GetBackpack()->Restore(Saved->bHasBackpack, Saved->bBackpackWorn, Saved->BackpackSlots, Saved->BackpackTransform);
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
#if !UE_BUILD_SHIPPING
	// -ErewhonGiveBackpack : tests, un sac garni (une cerpe sur le côté, de l'herbe dedans) ;
	// -ErewhonDropBackpack : posé aussitôt devant le joueur.
	UBackpackComponent* TestBag = PlayerState->GetBackpack();
	if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonGiveBackpack")) && !TestBag->IsOwned())
	{
		TestBag->Grant();
		TestBag->StoreItem(TEXT("cerpe"), 1, true);
		TestBag->StoreItem(TEXT("grass"), 7);
		TestBag->StoreItem(TEXT("fern"), 3);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonDropBackpack")) && TestBag->IsWorn())
	{
		TestBag->Drop(Controller->GetPawn());
	}
	// -ErewhonTestCart : une charrette chargée (une cagette pleine, un tonneau) et une pile de trois cagettes.
	if (APawn* TestPawn = Controller->GetPawn(); TestPawn && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestCart")))
	{
		const FVector Base = TestPawn->GetActorLocation() - FVector(0, 0, 88.f);
		const FTransform CartAt(FRotator(0.f, 90.f, 0.f), Base + FVector(400.f, 0.f, 0.f));
		ACart* Cart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), CartAt);
		Cart->Setup(Key, DisplayName);
		Cart->FinishSpawning(CartAt);
		const auto Spawn = [&](EStorageKind Kind, const FVector& At)
		{
			AStorageActor* Storage = GetWorld()->SpawnActorDeferred<AStorageActor>(AStorageActor::StaticClass(), FTransform(At));
			Storage->Setup(Kind, Key, DisplayName);
			Storage->FinishSpawning(FTransform(At));
			Storage->StoreItem(TEXT("grass"), 25);
			return Storage;
		};
		Spawn(EStorageKind::Crate, Base)->LoadInto(Cart, 0);
		Spawn(EStorageKind::Barrel, Base)->LoadInto(Cart, 1);
		AStorageActor* Bottom = Spawn(EStorageKind::Crate, Base + FVector(150.f, -250.f, 0.f));
		Bottom->PlaceOnGround(Base + FVector(150.f, -250.f, 0.f), 0.f);
		Spawn(EStorageKind::Crate, Base)->StackOn(Bottom);
		Spawn(EStorageKind::Crate, Base)->StackOn(Bottom);
		// -ErewhonTestDrive : le joueur prend aussitôt les brancards.
		if (AErewhonCharacter* Driver = Cast<AErewhonCharacter>(TestPawn); Driver && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestDrive")))
		{
			Cart->StartDriving(Driver);
			Driver->SetDrivenCart(Cart);
		}
	}
	// -ErewhonTestApothecary : le secteur 1:1 au joueur, avec ses chemins et son apothicairerie (feu allumé),
	// de quoi y travailler ; le joueur entre dans la maison.
	if (APawn* TestPawn = Controller->GetPawn(); TestPawn && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestApothecary")))
	{
		AErewhonGameState* TestState = GetGameState<AErewhonGameState>();
		const FIntPoint TestSector(1, 1);
		TestState->SetSectorOwner(TestSector, Key, DisplayName, (1 << 1) | (1 << 3), 0, ESectorBuilding::Apothecary);
		TArray<FInventorySlot> TestShelves;
		TestShelves.SetNum(ErewhonApothecary::ShelfSlots);
		const TPair<const TCHAR*, int32> Stocked[] = { { TEXT("potion_speed"), 10 }, { TEXT("potion_night"), 7 }, { TEXT("potion_grow"), 3 }, { TEXT("potion_jump"), 10 }, { TEXT("crushed_flower_purple"), 6 } };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Stocked); ++i)
		{
			TestShelves[(i * 3) % ErewhonApothecary::ShelfSlots + (i >= 4 ? 1 : 0)].ItemId = Stocked[i].Key;
			TestShelves[(i * 3) % ErewhonApothecary::ShelfSlots + (i >= 4 ? 1 : 0)].Count = Stocked[i].Value;
		}
		SpawnApothecary(TestSector, TestState->GetWorldClock() + 600.0, TestShelves);
		for (const TPair<const TCHAR*, int32>& Gift : { TPair<const TCHAR*, int32>(TEXT("firewood"), 3), { TEXT("grass"), 6 },
			{ TEXT("crushed_flower_blue"), 2 }, { TEXT("potion_base"), 1 }, { TEXT("potion_speed"), 1 }, { TEXT("potion_night"), 1 } })
		{
			Inventory->StoreItem(Gift.Key, Gift.Value);
		}
		FString TestMenu;
		const bool bAtManager = FParse::Value(FCommandLine::Get(), TEXT("ErewhonOpenMenu="), TestMenu) && TestMenu == TEXT("sectors");
		const FTransform HouseAt = ErewhonApothecary::HouseTransform(TestSector, TestState->GetWorldSeed());
		const FVector Inside = HouseAt.TransformPosition(FVector(-230.f, 0.f, ErewhonSpawn::CabinFloorTop + 100.f));
		// -ErewhonTestDrink=night,grow : ces potions bues aussitôt.
		FString Drinks;
		if (FParse::Value(FCommandLine::Get(), TEXT("ErewhonTestDrink="), Drinks, false))
		{
			TArray<FString> Names;
			Drinks.ParseIntoArray(Names, TEXT(","));
			for (const FString& Name : Names)
			{
				if (const FItemDef* Potion = UItemDatabase::Get(this)->Find(FName(TEXT("potion_") + Name)); Potion && Potion->IsPotion())
				{
					if (AErewhonCharacter* Drinker = Cast<AErewhonCharacter>(TestPawn))
					{
						Drinker->DrinkPotion(Potion->PotionEffect);
					}
				}
			}
		}
		if (!bAtManager)   // pour la carte du gérant, on reste au village
		{
			TestPawn->TeleportTo(Inside, HouseAt.Rotator());
			Controller->SetControlRotation(HouseAt.Rotator());
		}
	}
#endif
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

// ── Apothicairerie ───────────────────────────────────────────────────────────

AApothecary* AErewhonGameMode::SpawnApothecary(const FIntPoint& Sector, double FireUntil, const TArray<FInventorySlot>& Shelves)
{
	if (const TObjectPtr<AApothecary>* Existing = Apothecaries.Find(Sector); Existing && *Existing)
	{
		return *Existing;
	}
	const FTransform At = ErewhonApothecary::HouseTransform(Sector, GetGameState<AErewhonGameState>()->GetWorldSeed());
	AApothecary* House = GetWorld()->SpawnActorDeferred<AApothecary>(AApothecary::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	House->Setup(Sector, FireUntil, Shelves);
	House->FinishSpawning(At);
	Apothecaries.Add(Sector, House);
	return House;
}

FText AErewhonGameMode::TryBuyApothecary(AErewhonPlayerController* Controller, const FIntPoint& Sector)
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
	if (ErewhonSector::TypeAt(Sector, State->GetWorldSeed()) != ESectorType::Prairie)
	{
		return LOCTEXT("NotPrairie", "L'apothicairerie se construit dans une prairie.");
	}
	if (State->GetBuildings(Sector) & ESectorBuilding::Apothecary)
	{
		return LOCTEXT("AlreadyBuilt", "L'apothicairerie est déjà construite.");
	}
	if (!PlayerState->SpendCoins(UErewhonSettings::Get().ApothecaryPrice))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	State->AddBuilding(Sector, ESectorBuilding::Apothecary);
	SpawnApothecary(Sector, 0.0);
	Controller->ClientToast(LOCTEXT("ApothecaryBuilt", "Votre apothicairerie est construite, au centre du secteur."));
	return FText::GetEmpty();
}

FText AErewhonGameMode::CheckAtHouse(AErewhonPlayerController* Controller, const AApothecary* House) const
{
	const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const AErewhonGameState* State = GetGameState<AErewhonGameState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !PlayerState->IsRegistered() || !Pawn || !House)
	{
		return LOCTEXT("NoHouse", "Il n'y a pas d'apothicairerie ici.");
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(House->GetSector());
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey())
	{
		return LOCTEXT("NotYourHouse", "Ce n'est pas votre apothicairerie.");
	}
	if (FVector::Dist2D(Pawn->GetActorLocation(), House->GetActorLocation()) > ErewhonApothecary::UseReach)
	{
		return LOCTEXT("HouseTooFar", "Vous êtes trop loin de l'apothicairerie.");
	}
	return FText::GetEmpty();
}

namespace
{
	/** Exemplaires d'un objet dans les poches et le sac. */
	int32 CountOwned(const AErewhonPlayerState* PlayerState, FName ItemId)
	{
		return PlayerState->GetInventory()->CountStored(ItemId) + PlayerState->GetBackpack()->CountStored(ItemId);
	}

	/** Prend des exemplaires dans les poches, puis dans le sac. */
	bool TakeOwned(AErewhonPlayerState* PlayerState, FName ItemId, int32 Count)
	{
		if (CountOwned(PlayerState, ItemId) < Count)
		{
			return false;
		}
		const int32 FromPockets = FMath::Min(Count, PlayerState->GetInventory()->CountStored(ItemId));
		return (FromPockets <= 0 || PlayerState->GetInventory()->RemoveStored(ItemId, FromPockets))
			&& (Count - FromPockets <= 0 || PlayerState->GetBackpack()->RemoveStored(ItemId, Count - FromPockets));
	}

	/** Range un exemplaire dans les poches, sinon dans le sac. */
	bool GiveOwned(AErewhonPlayerState* PlayerState, FName ItemId)
	{
		if (PlayerState->GetInventory()->StoreItem(ItemId, 1) > 0)
		{
			return true;
		}
		return PlayerState->GetBackpack()->IsOwned() && PlayerState->GetBackpack()->StoreItem(ItemId, 1) > 0;
	}

	bool HasIngredients(const AErewhonPlayerState* PlayerState, const ErewhonApothecary::FRecipe& Recipe)
	{
		for (const TPair<FName, int32>& Input : Recipe.Inputs)
		{
			if (CountOwned(PlayerState, Input.Key) < Input.Value)
			{
				return false;
			}
		}
		return true;
	}
}

FText AErewhonGameMode::TryFeedFire(AErewhonPlayerController* Controller, AApothecary* House)
{
	const FText Error = CheckAtHouse(Controller, House);
	if (!Error.IsEmpty())
	{
		return Error;
	}
	if (House->FireRemaining() > ErewhonApothecary::MaxFireSeconds - ErewhonApothecary::FireSecondsPerWood)
	{
		return LOCTEXT("FireFull", "Le feu est déjà bien nourri.");
	}
	if (!TakeOwned(Controller->GetPlayerState<AErewhonPlayerState>(), TEXT("firewood"), 1))
	{
		return LOCTEXT("NoFirewood", "Il vous faut du petit bois (au General Store).");
	}
	House->AddFire(ErewhonApothecary::FireSecondsPerWood);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryStartCraft(AErewhonPlayerController* Controller, AApothecary* House, FName Output, int32 Count)
{
	const FText Error = CheckAtHouse(Controller, House);
	if (!Error.IsEmpty())
	{
		return Error;
	}
	const TOptional<ErewhonApothecary::FRecipe> Recipe = ErewhonApothecary::FindRecipe(*UItemDatabase::Get(this), Output);
	if (!Recipe.IsSet() || Count <= 0)
	{
		return LOCTEXT("NoRecipe", "Je ne sais pas préparer cela.");
	}
	if (Recipe->bCauldron && !House->IsFireLit())
	{
		return LOCTEXT("FireOut", "Le feu est éteint : allumez-le avec du petit bois.");
	}
	if (!HasIngredients(Controller->GetPlayerState<AErewhonPlayerState>(), *Recipe))
	{
		return LOCTEXT("MissingIngredients", "Il manque des ingrédients.");
	}
	// Un exemplaire à la fois : le premier sera prêt dans quelques secondes.
	FErewhonCraftJob Job;
	Job.Output = Output;
	Job.Remaining = FMath::Min(Count, 999);
	Job.StepSeconds = Recipe->Seconds();
	Job.StepEnd = GetGameState<AErewhonGameState>()->GetWorldClock() + Job.StepSeconds;
	Controller->SetCraftJob(Job, House);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	GetWorldTimerManager().SetTimer(Controller->CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
	{
		if (WeakController.IsValid())
		{
			StepCraft(WeakController.Get());
		}
	}), Job.StepSeconds, false);
	return FText::GetEmpty();
}

void AErewhonGameMode::StepCraft(AErewhonPlayerController* Controller)
{
	FErewhonCraftJob Job = Controller->GetCraftJob();
	AApothecary* House = Controller->GetCraftHouse();
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const TOptional<ErewhonApothecary::FRecipe> Recipe = ErewhonApothecary::FindRecipe(*UItemDatabase::Get(this), Job.Output);
	const auto Stop = [Controller](const FText& Why)
	{
		Controller->SetCraftJob(FErewhonCraftJob(), nullptr);
		if (!Why.IsEmpty())
		{
			Controller->ClientToast(Why);
		}
	};
	if (!Job.IsActive() || !Recipe.IsSet() || !PlayerState)
	{
		Stop(FText::GetEmpty());
		return;
	}
	const FText Error = CheckAtHouse(Controller, House);
	if (!Error.IsEmpty())
	{
		Stop(Error);
		return;
	}
	if (Recipe->bCauldron && !House->IsFireLit())
	{
		Stop(LOCTEXT("FireWentOut", "Le feu s'est éteint : le chaudron refroidit."));
		return;
	}
	if (!HasIngredients(PlayerState, *Recipe))
	{
		Stop(LOCTEXT("OutOfIngredients", "Plus assez d'ingrédients."));
		return;
	}
	for (const TPair<FName, int32>& Input : Recipe->Inputs)
	{
		TakeOwned(PlayerState, Input.Key, Input.Value);
	}
	if (!GiveOwned(PlayerState, Job.Output))
	{
		// Plus de place : les ingrédients retournent dans les poches (on vient de les y prendre).
		for (const TPair<FName, int32>& Input : Recipe->Inputs)
		{
			PlayerState->GetInventory()->StoreItem(Input.Key, Input.Value);
		}
		Stop(LOCTEXT("NoRoom", "Plus de place dans l'inventaire."));
		return;
	}
	Controller->ClientCraftStep(Recipe->bCauldron);

	// Le suivant, s'il en reste à faire et de quoi le faire.
	--Job.Remaining;
	if (Job.Remaining <= 0 || !HasIngredients(PlayerState, *Recipe))
	{
		Stop(FText::GetEmpty());
		return;
	}
	Job.StepEnd = GetGameState<AErewhonGameState>()->GetWorldClock() + Job.StepSeconds;
	Controller->SetCraftJob(Job, House);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	GetWorldTimerManager().SetTimer(Controller->CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
	{
		if (WeakController.IsValid())
		{
			StepCraft(WeakController.Get());
		}
	}), Job.StepSeconds, false);
}

namespace
{
	/** Pose des exemplaires sur une étagère : d'abord sur les piles du même objet, puis sur les étages vides, du bas vers le haut. Renvoie combien. */
	int32 ShelveItems(AApothecary* House, int32 Shelf, FName ItemId, int32 Count)
	{
		int32 Placed = 0;
		TArray<FInventorySlot>& Shelves = House->EditShelves();
		const int32 First = ErewhonApothecary::FirstSlotOf(Shelf);
		for (const bool bEmptyPass : { false, true })
		{
			for (int32 Index = First; Index < First + ErewhonApothecary::ShelfRows && Shelves.IsValidIndex(Index); ++Index)
			{
				FInventorySlot& Slot = Shelves[Index];
				if (Placed >= Count)
				{
					break;
				}
				if (bEmptyPass ? !Slot.IsEmpty() : (Slot.IsEmpty() || Slot.ItemId != ItemId))
				{
					continue;
				}
				const int32 Put = FMath::Min(ErewhonApothecary::PotionsPerSlot - (Slot.IsEmpty() ? 0 : Slot.Count), Count - Placed);
				if (Put > 0)
				{
					Slot.Count = (Slot.IsEmpty() ? 0 : Slot.Count) + Put;
					Slot.ItemId = ItemId;
					Placed += Put;
				}
			}
		}
		return Placed;
	}
}

FText AErewhonGameMode::TryMoveShelfItem(AErewhonPlayerController* Controller, AApothecary* House, int32 Shelf, bool bFromShelf, int32 From, bool bToShelf, int32 To)
{
	const FText Error = CheckAtHouse(Controller, House);
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!Error.IsEmpty() || !Items || Shelf < 0 || Shelf >= ErewhonApothecary::ShelfCount)
	{
		return Error;
	}
	// On ne touche qu'aux étages de l'étagère ouverte.
	const auto OnShelf = [Shelf](int32 Index) { return Index == INDEX_NONE || ErewhonApothecary::ShelfOfSlot(Index) == Shelf; };
	if ((bFromShelf && !OnShelf(From)) || (bToShelf && !OnShelf(To)))
	{
		return FText::GetEmpty();
	}
	UInventoryComponent* Inventory = Controller->GetPlayerState<AErewhonPlayerState>()->GetInventory();
	TArray<FInventorySlot>& Source = bFromShelf ? House->EditShelves() : Inventory->EditSlots();
	TArray<FInventorySlot>& Target = bToShelf ? House->EditShelves() : Inventory->EditSlots();
	if (!Source.IsValidIndex(From) || Source[From].IsEmpty() || (!bFromShelf && Source[From].Kind != ESlotKind::Inventory))
	{
		return FText::GetEmpty();
	}
	const FItemDef* Def = Items->Find(Source[From].ItemId);
	if (!Def)
	{
		return FText::GetEmpty();
	}
	if (bToShelf && !ErewhonApothecary::IsShelvable(*Def))
	{
		return LOCTEXT("ShelfOnly", "Les étagères sont pour les potions et les écrasés.");
	}
	// Une pile de 10 au plus par rayon.
	const auto Cap = [](bool bShelf, const FItemDef& Item) { return bShelf ? FMath::Min(Item.MaxStack, ErewhonApothecary::PotionsPerSlot) : Item.MaxStack; };
	if (To == INDEX_NONE)
	{
		const int32 Count = Source[From].Count;
		const int32 Moved = bToShelf ? ShelveItems(House, Shelf, Def->Id, Count) : Inventory->StoreItem(Def->Id, Count);
		if (Moved <= 0)
		{
			return bToShelf ? LOCTEXT("ShelvesFull", "Cette étagère est pleine.") : LOCTEXT("InventoryFull", "Inventaire plein.");
		}
		FInventorySlot& Origin = (bFromShelf ? House->EditShelves() : Inventory->EditSlots())[From];
		Origin.Count -= Moved;
		if (Origin.Count <= 0)
		{
			Origin.Clear();
		}
	}
	else
	{
		if (!Target.IsValidIndex(To) || (&Source == &Target && From == To) || (!bToShelf && Target[To].Kind != ESlotKind::Inventory))
		{
			return FText::GetEmpty();
		}
		FInventorySlot& A = Source[From];
		FInventorySlot& B = Target[To];
		if (B.IsEmpty() || B.ItemId == A.ItemId)
		{
			const int32 Moved = FMath::Min(A.Count, Cap(bToShelf, *Def) - (B.IsEmpty() ? 0 : B.Count));
			if (Moved <= 0)
			{
				return FText::GetEmpty();
			}
			B.Count = (B.IsEmpty() ? 0 : B.Count) + Moved;
			B.ItemId = A.ItemId;
			A.Count -= Moved;
			if (A.Count <= 0)
			{
				A.Clear();
			}
		}
		else
		{
			// Échange : chacun doit pouvoir aller de l'autre côté, sans dépasser 10 sur un rayon.
			const FItemDef* Other = Items->Find(B.ItemId);
			if (!Other || (bFromShelf && (!ErewhonApothecary::IsShelvable(*Other) || B.Count > Cap(true, *Other)))
				|| (bToShelf && A.Count > Cap(true, *Def)))
			{
				return LOCTEXT("CannotSwapShelf", "Cet objet ne va pas là.");
			}
			Swap(A.ItemId, B.ItemId);
			Swap(A.Count, B.Count);
		}
	}
	Inventory->CommitChanges();
	House->CommitShelves();
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryStashInShelves(AErewhonPlayerController* Controller, AApothecary* House, int32 Shelf)
{
	const FText Error = CheckAtHouse(Controller, House);
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!Error.IsEmpty() || !Items || Shelf < 0 || Shelf >= ErewhonApothecary::ShelfCount)
	{
		return Error;
	}
	UInventoryComponent* Inventory = Controller->GetPlayerState<AErewhonPlayerState>()->GetInventory();
	int32 Moved = 0;
	for (FInventorySlot& Slot : Inventory->EditSlots())
	{
		const FItemDef* Def = Slot.Kind == ESlotKind::Inventory && !Slot.IsEmpty() ? Items->Find(Slot.ItemId) : nullptr;
		if (!Def || !ErewhonApothecary::IsShelvable(*Def))
		{
			continue;
		}
		const int32 Placed = ShelveItems(House, Shelf, Def->Id, Slot.Count);
		Moved += Placed;
		Slot.Count -= Placed;
		if (Slot.Count <= 0)
		{
			Slot.Clear();
		}
	}
	Inventory->CommitChanges();
	House->CommitShelves();
	return Moved > 0 ? FText::GetEmpty() : LOCTEXT("NothingToShelve", "Rien à ranger (ou cette étagère est pleine).");
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
	// Ce qu'on y avait construit part avec le secteur.
	if (TObjectPtr<AApothecary> House; Apothecaries.RemoveAndCopyValue(Sector, House) && House)
	{
		House->Destroy();
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryBuyItem(AErewhonPlayerController* Controller, FName ItemId)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const FItemDef* Def = UItemDatabase::Get(this)->Find(ItemId);
	if (!PlayerState || !PlayerState->IsRegistered() || !Def || !IsNearMerchant(Controller, GetGameState<AErewhonGameState>()->ShopFor(*Def)))
	{
		return LOCTEXT("TooFar", "Le marchand est trop loin.");
	}
	if (!Def || Def->BuyPrice <= 0)
	{
		return LOCTEXT("NotSold", "Le marchand ne vend pas cet objet.");
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	if (Def->IsBackpack())
	{
		// Le sac se porte aussitôt sur le dos : il n'entre pas dans l'inventaire.
		UBackpackComponent* Bag = PlayerState->GetBackpack();
		if (Bag->IsOwned())
		{
			return FText::Format(LOCTEXT("AlreadyHave", "Vous possédez déjà : {0}."), Def->Name);
		}
		if (!PlayerState->SpendCoins(Def->BuyPrice))
		{
			return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
		}
		Bag->Grant();
		Controller->ClientToast(LOCTEXT("BagBought", "Le sac est sur votre dos. [F] pour le poser au sol, puis [E] pour l'ouvrir."));
		return FText::GetEmpty();
	}
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
	if (!PlayerState || !PlayerState->IsRegistered() || !Def || !IsNearMerchant(Controller, GetGameState<AErewhonGameState>()->ShopFor(*Def)))
	{
		return LOCTEXT("TooFar", "Le marchand est trop loin.");
	}
	const int32 Price = Def ? Def->ResalePrice(UErewhonSettings::Get().ResaleRatio) : 0;
	if (Price <= 0)
	{
		return LOCTEXT("NotBought", "Le marchand n'achète pas cet objet.");
	}
	// Près d'un marchand, le contenu de son sac à dos compte aussi : les poches d'abord, puis le sac.
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	UBackpackComponent* Bag = PlayerState->GetBackpack();
	const int32 FromPockets = FMath::Min(Count, Inventory->CountStored(ItemId));
	const int32 FromBag = FMath::Min(Count - FromPockets, Bag->CountStored(ItemId));
	const int32 Sold = FromPockets + FromBag;
	if (Sold <= 0 || (FromPockets > 0 && !Inventory->RemoveStored(ItemId, FromPockets)) || (FromBag > 0 && !Bag->RemoveStored(ItemId, FromBag)))
	{
		return FText::GetEmpty();
	}
	PlayerState->AddCoins(Price * Sold);
	return FText::GetEmpty();
}

// ── Sac à dos ────────────────────────────────────────────────────────────────

namespace
{
	/** Un slot peut-il recevoir cet objet ? Côté joueur : ses slots d'inventaire seulement. Côté sac : tout, sauf ses slots d'outils. */
	bool CanHold(const FInventorySlot& Slot, bool bBag, const FItemDef& Def)
	{
		if (Slot.Kind == ESlotKind::Inventory)
		{
			return true;
		}
		return bBag && Slot.Kind == ESlotKind::Equipment && Def.IsEquipable();
	}
}

FText AErewhonGameMode::TryDropBackpack(AErewhonPlayerController* Controller)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!PlayerState || !PlayerState->GetBackpack()->Drop(Controller->GetPawn()))
	{
		return FText::GetEmpty();
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryPickUpBackpack(AErewhonPlayerController* Controller, ABackpack* Backpack)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !Backpack || !Pawn)
	{
		return FText::GetEmpty();
	}
	if (!Backpack->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("NotYourBag", "Ce sac n'est pas à vous.");
	}
	if (FVector::Dist(Pawn->GetActorLocation(), Backpack->GetActorLocation()) > 400.f)
	{
		return FText::GetEmpty();
	}
	PlayerState->GetBackpack()->PickUp();
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryMoveItem(AErewhonPlayerController* Controller, bool bFromBag, int32 From, bool bToBag, int32 To)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!PlayerState || !Items)
	{
		return FText::GetEmpty();
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	UBackpackComponent* Bag = PlayerState->GetBackpack();
	if ((bFromBag || bToBag) && !Bag->IsAccessibleFrom(Controller->GetPawn()))
	{
		return LOCTEXT("BagTooFar", "Votre sac est trop loin.");
	}
	TArray<FInventorySlot>& Source = bFromBag ? Bag->EditSlots() : Inventory->EditSlots();
	TArray<FInventorySlot>& Target = bToBag ? Bag->EditSlots() : Inventory->EditSlots();
	if (!Source.IsValidIndex(From) || Source[From].IsEmpty() || (!bFromBag && Source[From].Kind != ESlotKind::Inventory))
	{
		return FText::GetEmpty();
	}
	const FItemDef* Def = Items->Find(Source[From].ItemId);
	if (!Def)
	{
		return FText::GetEmpty();
	}

	if (To == INDEX_NONE)
	{
		// Clic : l'objet change de côté et se range où il peut.
		const int32 Count = Source[From].Count;
		const int32 Stored = bToBag ? Bag->StoreItem(Def->Id, Count, true) : Inventory->StoreItem(Def->Id, Count);
		if (Stored <= 0)
		{
			return bToBag ? LOCTEXT("BagFull", "Le sac est plein.") : LOCTEXT("InventoryFull", "Inventaire plein.");
		}
		FInventorySlot& Origin = (bFromBag ? Bag->EditSlots() : Inventory->EditSlots())[From];
		Origin.Count -= Stored;
		if (Origin.Count <= 0)
		{
			Origin.Clear();
		}
	}
	else
	{
		if (!Target.IsValidIndex(To) || (&Source == &Target && From == To) || !CanHold(Target[To], bToBag, *Def))
		{
			return FText::GetEmpty();
		}
		FInventorySlot& A = Source[From];
		FInventorySlot& B = Target[To];
		if (B.IsEmpty() || B.ItemId == A.ItemId)
		{
			// Dépôt, ou complément d'une pile.
			const int32 Moved = FMath::Min(A.Count, Def->MaxStack - (B.IsEmpty() ? 0 : B.Count));
			if (Moved <= 0)
			{
				return FText::GetEmpty();
			}
			B.ItemId = A.ItemId;
			B.Count = (B.IsEmpty() ? 0 : B.Count) + Moved;
			A.Count -= Moved;
			if (A.Count <= 0)
			{
				A.Clear();
			}
		}
		else
		{
			// Échange, si chacun des deux objets va bien dans l'autre slot.
			const FItemDef* Other = Items->Find(B.ItemId);
			if (!Other || !CanHold(A, bFromBag, *Other))
			{
				return LOCTEXT("CannotSwap", "Cet objet ne va pas là.");
			}
			Swap(A.ItemId, B.ItemId);
			Swap(A.Count, B.Count);
		}
	}
	Inventory->CommitChanges();
	if (Bag->IsOwned())
	{
		Bag->Commit();
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryStashAll(AErewhonPlayerController* Controller)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!PlayerState)
	{
		return FText::GetEmpty();
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	UBackpackComponent* Bag = PlayerState->GetBackpack();
	if (!Bag->IsAccessibleFrom(Controller->GetPawn()))
	{
		return LOCTEXT("BagTooFar", "Votre sac est trop loin.");
	}
	// Tout ce qui est rangé dans les poches (pas ce qu'on porte ni ce qu'on tient) part dans le sac.
	int32 Moved = 0;
	bool bLeftovers = false;
	for (FInventorySlot& Slot : Inventory->EditSlots())
	{
		if (Slot.Kind != ESlotKind::Inventory || Slot.IsEmpty())
		{
			continue;
		}
		const int32 Stored = Bag->StoreItem(Slot.ItemId, Slot.Count, true);
		Moved += Stored;
		Slot.Count -= Stored;
		if (Slot.Count <= 0)
		{
			Slot.Clear();
		}
		else
		{
			bLeftovers = true;
		}
	}
	Inventory->CommitChanges();
	if (Moved == 0)
	{
		return bLeftovers ? LOCTEXT("BagFull", "Le sac est plein.") : LOCTEXT("NothingToStash", "Rien à ranger.");
	}
	return bLeftovers ? LOCTEXT("BagFullPartly", "Le sac est plein : tout n'y est pas entré.") : FText::GetEmpty();
}

// ── Triche ───────────────────────────────────────────────────────────────────

namespace
{
	/** En partie publiée, seul l'hôte (le joueur du serveur) peut tricher ; en développement, tout le monde. */
	bool CanCheat(const AErewhonPlayerController* Controller)
	{
#if UE_BUILD_SHIPPING
		return Controller && Controller->IsLocalController();
#else
		return Controller != nullptr;
#endif
	}
}

FText AErewhonGameMode::TryCheatSetCoins(AErewhonPlayerController* Controller, int32 Coins)
{
	AErewhonPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (!CanCheat(Controller) || !PlayerState)
	{
		return LOCTEXT("NoCheat", "La triche est réservée à l'hôte de la partie.");
	}
	PlayerState->SetCoins(FMath::Clamp(Coins, 0, 1000000000));
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryCheatAdvanceTime(AErewhonPlayerController* Controller, float Seconds)
{
	if (!CanCheat(Controller))
	{
		return LOCTEXT("NoCheat", "La triche est réservée à l'hôte de la partie.");
	}
	GetGameState<AErewhonGameState>()->AdvanceClock(FMath::Clamp(Seconds, 0.f, 86400.f));
	return FText::GetEmpty();
}

// ── Village ──────────────────────────────────────────────────────────────────

FText AErewhonGameMode::TryBuyVillageUpgrade(AErewhonPlayerController* Controller, uint8 UpgradeIndex)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "Le gérant des secteurs est trop loin.");
	}
	if (UpgradeIndex >= static_cast<uint8>(EVillageUpgrade::Count))
	{
		return FText::GetEmpty();
	}
	const EVillageUpgrade Upgrade = static_cast<EVillageUpgrade>(UpgradeIndex);
	const ErewhonVillage::FUpgradeInfo& Info = ErewhonVillage::Info(Upgrade);
	if (State->HasVillageUpgrade(Upgrade))
	{
		return LOCTEXT("AlreadyBuilt", "C'est déjà fait !");
	}
	if (Info.Requires != EVillageUpgrade::Count && !State->HasVillageUpgrade(Info.Requires))
	{
		return FText::Format(LOCTEXT("RequiresFirst", "Il faut d'abord : {0}."), ErewhonVillage::Info(Info.Requires).Name);
	}
	if (!PlayerState->SpendCoins(Info.Price))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	State->AddVillageUpgrade(Upgrade);
	SaveWorld();
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryBuyPavedLink(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 Side)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "Le gérant des secteurs est trop loin.");
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey() || Side < 0 || Side >= 4)
	{
		return LOCTEXT("NotYours", "Ce secteur ne vous appartient pas.");
	}
	if (!State->IsVillagePaved())
	{
		return LOCTEXT("VillageNotPaved", "Le village doit d'abord être pavé.");
	}
	if (!(State->GetRoadLinks(Sector) & (1 << Side)) || (State->GetPavedLinks(Sector) & (1 << Side)))
	{
		return FText::GetEmpty();
	}
	if (!PlayerState->SpendCoins(UErewhonSettings::Get().PavedLinkPrice))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	State->AddPavedLink(Sector, Side);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryDiscard(AErewhonPlayerController* Controller, bool bFromBag, int32 SlotIndex, AStorageActor* Storage)
{
	if (Storage)
	{
		const AErewhonPlayerState* Player = Controller->GetPlayerState<AErewhonPlayerState>();
		const APawn* Pawn = Controller->GetPawn();
		if (!Player || !Pawn || !Storage->IsOwnedBy(Player->GetPlayerKey()) || FVector::Dist(Pawn->GetActorLocation(), Storage->GetActorLocation()) > 450.f
			|| !Storage->GetSlots().IsValidIndex(SlotIndex) || Storage->GetSlots()[SlotIndex].IsEmpty())
		{
			return FText::GetEmpty();
		}
		FInventorySlot& Slot = Storage->EditSlots()[SlotIndex];
		if (--Slot.Count <= 0)
		{
			Slot.Clear();
		}
		Storage->Commit();
		return FText::GetEmpty();
	}
	// Jeter un exemplaire de l'objet : de ses poches, de ses mains, ou du sac posé à côté.
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!PlayerState)
	{
		return FText::GetEmpty();
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	UBackpackComponent* Bag = PlayerState->GetBackpack();
	if (bFromBag && !Bag->IsAccessibleFrom(Controller->GetPawn()))
	{
		return LOCTEXT("BagTooFar", "Votre sac est trop loin.");
	}
	TArray<FInventorySlot>& Slots = bFromBag ? Bag->EditSlots() : Inventory->EditSlots();
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return FText::GetEmpty();
	}
	FInventorySlot& Slot = Slots[SlotIndex];
	if (Slot.Kind == ESlotKind::Clothing)
	{
		return LOCTEXT("TakeOffFirst", "Retirez d'abord ce vêtement.");
	}
	if (--Slot.Count <= 0)
	{
		Slot.Clear();
	}
	if (bFromBag)
	{
		Bag->Commit();
	}
	else
	{
		Inventory->CommitChanges();
	}
	return FText::GetEmpty();
}

// ── Charrette et conteneurs ──────────────────────────────────────────────────

namespace
{
	bool CanReachStorage(const AErewhonPlayerState* Owner, const APawn* Pawn, const AStorageActor* Storage)
	{
		return Owner && Pawn && Storage && Storage->IsOwnedBy(Owner->GetPlayerKey())
			&& (Storage->GetCarrier() == nullptr || Storage->GetCarrier() == Pawn)
			&& FVector::Dist(Pawn->GetActorLocation(), Storage->GetActorLocation()) <= 450.f;
	}
}

FText AErewhonGameMode::TryPickUpStorage(AErewhonPlayerController* Controller, AStorageActor* Storage)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonCharacter* Character = Controller->GetPawn<AErewhonCharacter>();
	if (!PlayerState || !Character || !Storage)
	{
		return FText::GetEmpty();
	}
	if (!Storage->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("NotYourStorage", "Ce n'est pas à vous.");
	}
	if (Character->GetCarried() || Character->GetDrivenCart())
	{
		return LOCTEXT("HandsFull", "Vous avez déjà les mains prises.");
	}
	if (!PlayerState->GetInventory()->GetHandItem().IsNone())
	{
		return LOCTEXT("FreeHands", "Il faut avoir les mains libres pour le porter.");
	}
	// D'une pile, on ne prend que la cagette du dessus.
	AStorageActor* Top = Storage->TopOfStack();
	if (!CanReachStorage(PlayerState, Character, Top))
	{
		return FText::GetEmpty();
	}
	Top->CarryBy(Character);
	Character->SetCarried(Top);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryPlace(AErewhonPlayerController* Controller, uint8 Type, const FVector& Location, float Yaw, AActor* Target, int32 Slot)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonCharacter* Character = Controller->GetPawn<AErewhonCharacter>();
	if (!PlayerState || !Character)
	{
		return FText::GetEmpty();
	}
	const FVector Where = Type == 0 ? Location : (Target ? Target->GetActorLocation() : Location);
	if (FVector::Dist(Character->GetActorLocation(), Where) > 700.f)
	{
		return LOCTEXT("TooFarToPlace", "C'est trop loin.");
	}

	// Ce qu'on pose : le conteneur qu'on porte, ou l'objet qu'on tient en main.
	AStorageActor* Storage = Character->GetCarried();
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	const FName HandItem = Inventory->GetHandItem();
	const bool bFromHand = !Storage;
	if (bFromHand && ErewhonPlaceables::IsCart(HandItem))
	{
		if (Type != 0)
		{
			return FText::GetEmpty();
		}
		ACart* Cart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), Location));
		Cart->Setup(PlayerState->GetPlayerKey(), PlayerState->GetPlayerName());
		Cart->FinishSpawning(FTransform(FRotator(0.f, Yaw, 0.f), Location));
	}
	else
	{
		const TOptional<EStorageKind> NewKind = bFromHand ? ErewhonPlaceables::StorageForItem(HandItem) : TOptional<EStorageKind>();
		if (bFromHand && !NewKind.IsSet())
		{
			return FText::GetEmpty();
		}
		const EStorageKind Kind = Storage ? Storage->GetKind() : NewKind.GetValue();
		AStorageActor* Base = Type == 1 ? Cast<AStorageActor>(Target) : nullptr;
		ACart* Cart = Type == 2 ? Cast<ACart>(Target) : nullptr;
		if (Type == 1 && (!Base || Kind != EStorageKind::Crate || Base->GetKind() != EStorageKind::Crate || !Base->IsOwnedBy(PlayerState->GetPlayerKey())
			|| Base->TopOfStack()->StackSize() >= ErewhonPlaceables::MaxStack || Base->TopOfStack()->GetCarrier()))
		{
			return LOCTEXT("CannotStack", "On ne peut pas l'empiler là.");
		}
		if (Type == 2 && (!Cart || !Cart->IsOwnedBy(PlayerState->GetPlayerKey()) || Cart->GetLoad(Slot) || Slot < 0 || Slot >= ACart::SlotCount))
		{
			return LOCTEXT("CartSlotTaken", "Cet emplacement n'est pas libre.");
		}
		if (!Storage)
		{
			const FTransform Spawn(FRotator(0.f, Yaw, 0.f), Location);
			Storage = GetWorld()->SpawnActorDeferred<AStorageActor>(AStorageActor::StaticClass(), Spawn);
			Storage->Setup(Kind, PlayerState->GetPlayerKey(), PlayerState->GetPlayerName());
			Storage->FinishSpawning(Spawn);
		}
		Character->SetCarried(nullptr);
		if (Type == 1)
		{
			Storage->StackOn(Base);
		}
		else if (Type == 2)
		{
			Storage->LoadInto(Cart, Slot);
		}
		else
		{
			Storage->PlaceOnGround(Location, Yaw);
		}
	}

	if (bFromHand)
	{
		// L'objet quitte la main.
		for (FInventorySlot& HandSlot : Inventory->EditSlots())
		{
			if (HandSlot.Kind == ESlotKind::Equipment && HandSlot.ProvidedBy == EClothingSlot::None)
			{
				if (--HandSlot.Count <= 0)
				{
					HandSlot.Clear();
				}
				break;
			}
		}
		Inventory->CommitChanges();
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryToggleDrive(AErewhonPlayerController* Controller, ACart* Cart)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonCharacter* Character = Controller->GetPawn<AErewhonCharacter>();
	if (!PlayerState || !Character)
	{
		return FText::GetEmpty();
	}
	if (ACart* Driven = Character->GetDrivenCart())
	{
		Driven->StopDriving();
		Character->SetDrivenCart(nullptr);
		return FText::GetEmpty();
	}
	if (!Cart || Cart->GetDriver())
	{
		return FText::GetEmpty();
	}
	if (!Cart->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("NotYourCart", "Cette charrette n'est pas à vous.");
	}
	if (Character->GetCarried())
	{
		return LOCTEXT("PutDownFirst", "Posez d'abord ce que vous portez.");
	}
	if (!Cart->IsAtHandles(Character->GetActorLocation()))
	{
		return LOCTEXT("GoToHandles", "Placez-vous devant les brancards.");
	}
	Cart->StartDriving(Character);
	Character->SetDrivenCart(Cart);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryMoveStorageItem(AErewhonPlayerController* Controller, AStorageActor* Storage, bool bFromStorage, int32 From, bool bToStorage, int32 To)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!PlayerState || !Items || !CanReachStorage(PlayerState, Controller->GetPawn(), Storage))
	{
		return LOCTEXT("StorageTooFar", "C'est trop loin.");
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	TArray<FInventorySlot>& Source = bFromStorage ? Storage->EditSlots() : Inventory->EditSlots();
	TArray<FInventorySlot>& Target = bToStorage ? Storage->EditSlots() : Inventory->EditSlots();
	if (!Source.IsValidIndex(From) || Source[From].IsEmpty() || (!bFromStorage && Source[From].Kind != ESlotKind::Inventory))
	{
		return FText::GetEmpty();
	}
	const FItemDef* Def = Items->Find(Source[From].ItemId);
	if (!Def)
	{
		return FText::GetEmpty();
	}
	if (bToStorage && !Storage->Accepts(*Def))
	{
		return LOCTEXT("PlantsOnly", "On n'y range que des plantes.");
	}
	if (To == INDEX_NONE)
	{
		const int32 Count = Source[From].Count;
		const int32 Stored = bToStorage ? Storage->StoreItem(Def->Id, Count) : Inventory->StoreItem(Def->Id, Count);
		if (Stored <= 0)
		{
			return bToStorage ? LOCTEXT("StorageFull", "C'est plein.") : LOCTEXT("InventoryFull", "Inventaire plein.");
		}
		FInventorySlot& Origin = (bFromStorage ? Storage->EditSlots() : Inventory->EditSlots())[From];
		Origin.Count -= Stored;
		if (Origin.Count <= 0)
		{
			Origin.Clear();
		}
	}
	else
	{
		if (!Target.IsValidIndex(To) || (&Source == &Target && From == To) || (!bToStorage && Target[To].Kind != ESlotKind::Inventory))
		{
			return FText::GetEmpty();
		}
		FInventorySlot& A = Source[From];
		FInventorySlot& B = Target[To];
		if (B.IsEmpty() || B.ItemId == A.ItemId)
		{
			const int32 Moved = FMath::Min(A.Count, Def->MaxStack - (B.IsEmpty() ? 0 : B.Count));
			if (Moved <= 0)
			{
				return FText::GetEmpty();
			}
			B.ItemId = A.ItemId;
			B.Count = (B.IsEmpty() ? 0 : B.Count) + Moved;
			A.Count -= Moved;
			if (A.Count <= 0)
			{
				A.Clear();
			}
		}
		else
		{
			const FItemDef* Other = Items->Find(B.ItemId);
			if (!Other || (bFromStorage && !Storage->Accepts(*Other)))
			{
				return LOCTEXT("CannotSwap", "Cet objet ne va pas là.");
			}
			Swap(A.ItemId, B.ItemId);
			Swap(A.Count, B.Count);
		}
	}
	Inventory->CommitChanges();
	Storage->Commit();
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryStashInStorage(AErewhonPlayerController* Controller, AStorageActor* Storage)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!CanReachStorage(PlayerState, Controller->GetPawn(), Storage))
	{
		return FText::GetEmpty();
	}
	// Toutes les plantes des poches y passent.
	int32 Moved = 0;
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	for (FInventorySlot& Slot : Inventory->EditSlots())
	{
		if (Slot.Kind != ESlotKind::Inventory || Slot.IsEmpty())
		{
			continue;
		}
		const int32 Stored = Storage->StoreItem(Slot.ItemId, Slot.Count);
		Moved += Stored;
		Slot.Count -= Stored;
		if (Slot.Count <= 0)
		{
			Slot.Clear();
		}
	}
	Inventory->CommitChanges();
	return Moved > 0 ? FText::GetEmpty() : LOCTEXT("NoPlantsToStash", "Aucune plante à ranger (ou c'est plein).");
}

void AErewhonGameMode::ReleaseHeld(APawn* Pawn)
{
	AErewhonCharacter* Character = Cast<AErewhonCharacter>(Pawn);
	if (!Character)
	{
		return;
	}
	if (AStorageActor* Carried = Character->GetCarried())
	{
		const FVector Feet = Character->GetActorLocation() + Character->GetActorForwardVector() * 60.f - FVector(0, 0, 88.f);
		Carried->PlaceOnGround(Feet, Character->GetActorRotation().Yaw);
		Character->SetCarried(nullptr);
	}
	if (ACart* Cart = Character->GetDrivenCart())
	{
		Cart->StopDriving();
		Character->SetDrivenCart(nullptr);
	}
}

void AErewhonGameMode::CaptureObjects()
{
	// Charrettes et conteneurs, avec leurs liens : pile, charrette. Ce qu'on porte est posé au sol.
	WorldSave->Carts.Reset();
	WorldSave->Storages.Reset();
	TMap<const ACart*, int32> CartIndex;
	for (TActorIterator<ACart> It(GetWorld()); It; ++It)
	{
		CartIndex.Add(*It, WorldSave->Carts.Num());
		WorldSave->Carts.Add({ It->GetOwnerKey(), It->GetOwnerName(), FTransform(FRotator(0.f, It->GetActorRotation().Yaw, 0.f), It->GetActorLocation()) });
	}
	// Du bas des piles vers le haut : une cagette est toujours enregistrée après celle qui la porte.
	TArray<AStorageActor*> Ordered;
	for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It)
	{
		if (!It->GetBelow())
		{
			for (AStorageActor* S = *It; S; S = S->GetAbove())
			{
				Ordered.Add(S);
			}
		}
	}
	TMap<const AStorageActor*, int32> StorageIndex;
	for (AStorageActor* Storage : Ordered)
	{
		FSavedStorage& Saved = WorldSave->Storages.AddDefaulted_GetRef();
		StorageIndex.Add(Storage, WorldSave->Storages.Num() - 1);
		Saved.Kind = static_cast<uint8>(Storage->GetKind());
		Saved.OwnerKey = Storage->GetOwnerKey();
		Saved.OwnerName = Storage->GetOwnerName();
		Saved.Slots = Storage->GetSlots();
		FVector Location = Storage->GetActorLocation();
		if (const APawn* Carrier = Storage->GetCarrier())
		{
			Location = Carrier->GetActorLocation() + Carrier->GetActorForwardVector() * 60.f - FVector(0, 0, 88.f);
		}
		Saved.Transform = FTransform(FRotator(0.f, Storage->GetActorRotation().Yaw, 0.f), Location);
		if (const int32* Cart = Storage->GetCart() ? CartIndex.Find(Storage->GetCart()) : nullptr)
		{
			Saved.Cart = *Cart;
			Saved.CartSlot = Storage->GetCartSlot();
		}
		if (const int32* Below = Storage->GetBelow() ? StorageIndex.Find(Storage->GetBelow()) : nullptr)
		{
			Saved.Below = *Below;
		}
	}
}

void AErewhonGameMode::SpawnSavedObjects()
{
	TArray<ACart*> Carts;
	for (const FSavedCart& Saved : WorldSave->Carts)
	{
		ACart* Cart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), Saved.Transform);
		Cart->Setup(Saved.OwnerKey, Saved.OwnerName);
		Cart->FinishSpawning(Saved.Transform);
		Carts.Add(Cart);
	}
	TArray<AStorageActor*> Storages;
	for (const FSavedStorage& Saved : WorldSave->Storages)
	{
		AStorageActor* Storage = GetWorld()->SpawnActorDeferred<AStorageActor>(AStorageActor::StaticClass(), Saved.Transform);
		Storage->Setup(static_cast<EStorageKind>(Saved.Kind), Saved.OwnerKey, Saved.OwnerName);
		for (int32 i = 0; i < Saved.Slots.Num() && i < Storage->EditSlots().Num(); ++i)
		{
			Storage->EditSlots()[i] = Saved.Slots[i];
		}
		Storage->FinishSpawning(Saved.Transform);
		Storages.Add(Storage);
		if (Carts.IsValidIndex(Saved.Cart))
		{
			Storage->LoadInto(Carts[Saved.Cart], Saved.CartSlot);
		}
		else if (Storages.IsValidIndex(Saved.Below) && Saved.Below < Storages.Num() - 1)
		{
			Storage->StackOn(Storages[Saved.Below]);
		}
	}
}

#undef LOCTEXT_NAMESPACE
