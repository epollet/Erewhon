#include "Core/ErewhonGameMode.h"

#include "World/Villagers.h"
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
#include "Components/CapsuleComponent.h"
#include "Inventory/BackpackComponent.h"
#include "World/Backpack.h"
#include "World/Village.h"
#include "World/Storage.h"
#include "Player/ErewhonCharacter.h"
#include "EngineUtils.h"
#include "World/SpawnLayout.h"
#include "World/Apothecary.h"
#include "World/Farm.h"
#include "World/Forestry.h"
#include "World/FelledTree.h"
#include "World/Bhurak.h"
#include "World/DroppedItem.h"
#include "World/VillageNetwork.h"

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
	for (const FSavedVillage& Saved : WorldSave->Villages)
	{
		State->SetVillageUpgrades(Saved.Village, Saved.Upgrades);
	}
#if !UE_BUILD_SHIPPING
	// -ErewhonVillage=<masque> : tests, le village de départ avec ces améliorations (-1 : toutes).
	int32 TestUpgrades = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("ErewhonVillage="), TestUpgrades))
	{
		State->SetVillageUpgrades(ErewhonVillages::StartVillage, TestUpgrades);
	}
#endif
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
		if (Now - Harvest.HarvestTime < ErewhonForestry::RegrowSeconds(Harvest.PlantIndex))
		{
			State->AddHarvest(Harvest.Sector, Harvest.PlantIndex, Harvest.HarvestTime);
		}
	}
	for (const FSavedField& Field : WorldSave->Fields)
	{
		if (ErewhonFarm::IsFieldCell(Field.Sector, State->GetBuildings(Field.Sector), Field.Cell))
		{
			State->SetFieldCell(Field.Sector, Field.Cell, Field.Crop, Field.SownAt);
		}
	}
}

void AErewhonGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Les portes des cabanes et les habitants sont des acteurs répliqués : leur état est partagé par tous.
	// Ceux d'un village viennent quand un joueur en approche (voir StreamVillageActors).
	StreamVillageActors();
	GetWorldTimerManager().SetTimer(VillageStreamTimer, this, &AErewhonGameMode::StreamVillageActors, 1.f, true);
	SpawnSavedObjects();
	// Les apothicaireries construites, avec leur feu tel qu'on l'a laissé.
	for (const FSectorOwnerEntry& Entry : GetGameState<AErewhonGameState>()->GetOwnerEntries())
	{
		if (Entry.Buildings & ESectorBuilding::Apothecary)
		{
			const FSavedSectorOwner* Saved = WorldSave->SectorOwners.FindByPredicate([&](const FSavedSectorOwner& S) { return S.Sector == Entry.Sector; });
			SpawnApothecary(Entry.Sector, Saved ? Saved->FireUntil : 0.0);
			// Une apothicairerie d'avant le mobilier à acheter : on la meuble comme elle l'était.
			if (Saved && Saved->Shelves.Num() > 0)
			{
				SpawnFurnishing(Entry.Sector, Entry.OwnerKey, Entry.OwnerName, Saved->Shelves);
			}
		}
	}
	GetGameState<AErewhonGameState>()->OnVillageChanged.AddUObject(this, &AErewhonGameMode::SpawnDoors);
	GetWorldTimerManager().SetTimer(FountainTimer, this, &AErewhonGameMode::FillAtFountain, 0.25f, true);
	GetWorldTimerManager().SetTimer(AutosaveTimer, this, &AErewhonGameMode::SaveWorld,
		UErewhonSettings::Get().AutosaveIntervalSeconds, true);
}

void AErewhonGameMode::StreamVillageActors()
{
	// Les villages à moins de quatre secteurs d'un joueur (les habitants s'y promènent jusqu'à un secteur et
	// demi de la place) ; repartis au-delà de cinq. Les cabanes, locales, sont chargées plus loin (voir
	// AErewhonGameState::StreamVillages) : la porte arrive toujours dans une cabane déjà là.
	constexpr int32 LoadRadius = 4;
	constexpr int32 KeepRadius = 5;
	TArray<FIntPoint> Focus;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Player = It->Get();
		if (const APawn* Pawn = Player ? Player->GetPawn() : nullptr)
		{
			Focus.Add(ErewhonSector::FromWorld(Pawn->GetActorLocation()));
		}
	}
	TSet<FIntPoint> Wanted;
	for (const FIntPoint& Sector : Focus)
	{
		TArray<FIntPoint> Near;
		ErewhonVillages::VillagesIn(Sector - FIntPoint(LoadRadius), Sector + FIntPoint(LoadRadius), Near);
		Wanted.Append(Near);
	}
	for (const FIntPoint& Village : Wanted)
	{
		if (!ServerVillages.Contains(Village))
		{
			SpawnVillageActors(Village);
		}
	}
	TArray<FIntPoint> Far;
	for (const TPair<FIntPoint, FServerVillage>& Pair : ServerVillages)
	{
		const FIntPoint Center = ErewhonVillages::CenterOf(Pair.Key);
		const bool bKept = Focus.ContainsByPredicate([&Center](const FIntPoint& Sector)
		{
			return FMath::Max(FMath::Abs(Sector.X - Center.X), FMath::Abs(Sector.Y - Center.Y)) <= KeepRadius;
		});
		if (!bKept)
		{
			Far.Add(Pair.Key);
		}
	}
	for (const FIntPoint& Village : Far)
	{
		DespawnVillageActors(Village);
	}
}

void AErewhonGameMode::SpawnVillageActors(const FIntPoint& Village)
{
	FServerVillage& Streamed = ServerVillages.Add(Village);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Les maisons des habitants : leur porte, et chacun chez soi.
	for (int32 House = 0; House < ErewhonVillagers::HouseCount; ++House)
	{
		const FTransform HouseT = ErewhonVillagers::HouseTransform(Village, House);
		const FTransform Hinge = FTransform(FVector(-ErewhonSpawn::CabinHalfLength + ErewhonSpawn::CabinWallThickness * 0.5f,
			-ErewhonSpawn::DoorHalfWidth, ErewhonSpawn::CabinFloorTop)) * HouseT;
		ADoor* Door = GetWorld()->SpawnActor<ADoor>(Hinge.GetLocation(), Hinge.Rotator(), Params);
		const FTransform At(HouseT.Rotator(), HouseT.TransformPosition(FVector(60.f, 0.f, ErewhonSpawn::CabinFloorTop + 90.f)));
		AVillager* Villager = GetWorld()->SpawnActorDeferred<AVillager>(AVillager::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Villager->ResidentIndex = House;
		Villager->Village = Village;
		Villager->HouseDoor = Door;
		Villager->FinishSpawning(At);
		Streamed.Residents.Add(Door);
		Streamed.Residents.Add(Villager);
	}
	// Les portes des commerces bâtis.
	SpawnDoors(Village);
}

void AErewhonGameMode::DespawnVillageActors(const FIntPoint& Village)
{
	FServerVillage Streamed;
	if (!ServerVillages.RemoveAndCopyValue(Village, Streamed))
	{
		return;
	}
	TArray<TWeakObjectPtr<AActor>> Actors;
	Streamed.Doors.GenerateValueArray(Actors);
	Actors.Append(Streamed.Residents);
	for (const TWeakObjectPtr<AActor>& Actor : Actors)
	{
		if (Actor.IsValid())
		{
			Actor->Destroy();
		}
	}
}

void AErewhonGameMode::SpawnDoors(const FIntPoint& Village)
{
	FServerVillage* Streamed = ServerVillages.Find(Village);
	if (!Streamed)
	{
		return;   // personne près de ce village : ses portes viendront avec le premier qui en approche
	}
	const AErewhonGameState* State = GetGameState<AErewhonGameState>();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const EShopKind Kind : ErewhonSpawn::ShopKinds)
	{
		if (Streamed->Doors.Contains(Kind) || !State->IsShopBuilt(Village, Kind))
		{
			continue;
		}
		const FTransform Hinge = ErewhonSpawn::DoorHingeTransform(Village, Kind);
		Streamed->Doors.Add(Kind, GetWorld()->SpawnActor<ADoor>(Hinge.GetLocation(), Hinge.Rotator(), Params));
	}
}

FIntPoint AErewhonGameMode::PlayerVillage(const AController* Controller) const
{
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	return Pawn ? ErewhonSpawn::VillageAt(Pawn->GetActorLocation()) : ErewhonVillages::StartVillage;
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
	WorldSave->Villages.Reset();
	for (const FVillageUpgradesEntry& Entry : State->GetVillageUpgradeEntries())
	{
		if (Entry.Upgrades != 0)
		{
			WorldSave->Villages.Add({ Entry.Village, Entry.Upgrades });
		}
	}
	CaptureObjects();
	WorldSave->SectorOwners.Reset();
	for (const FSectorOwnerEntry& Entry : State->GetOwnerEntries())
	{
		const TObjectPtr<AApothecary>* House = Apothecaries.Find(Entry.Sector);
		WorldSave->SectorOwners.Add({ Entry.Sector, Entry.OwnerKey, Entry.OwnerName, Entry.RoadLinks, Entry.PavedLinks, Entry.Buildings,
			House && *House ? (*House)->GetFireUntil() : 0.0, TArray<FInventorySlot>() });
	}
	WorldSave->Harvests.Reset();
	for (const FHarvestEntry& Entry : State->GetHarvestEntries())
	{
		WorldSave->Harvests.Add({ Entry.Sector, Entry.PlantIndex, Entry.HarvestTime });
	}
	WorldSave->Fields.Reset();
	for (const FFieldCellEntry& Entry : State->GetFieldEntries())
	{
		WorldSave->Fields.Add({ Entry.Sector, Entry.Cell, Entry.Crop, Entry.SownAt });
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
	Saved.PlayerClass = static_cast<uint8>(PlayerState->GetPlayerClass());
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
		PlayerState->SetPlayerClass(Saved->PlayerClass <= static_cast<uint8>(EPlayerClass::Forester) ? static_cast<EPlayerClass>(Saved->PlayerClass) : EPlayerClass::None);
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
	// -ErewhonTestFountain : un tonneau vide sous le bec de la fontaine de la place.
	if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestFountain")))
	{
		const FVector Spot = ErewhonVillage::FountainBarrelSpot(ErewhonVillages::StartVillage);
		const FVector At(Spot.X, Spot.Y, ErewhonTerrain::Height(FVector2D(Spot), GetGameState<AErewhonGameState>()->GetWorldSeed()));
		SpawnStorage(EStorageKind::Barrel, FTransform(At), Key, DisplayName);
	}
	// -ErewhonTestForest : le premier secteur boisé près de la place au joueur, devenu forestier, hache en main,
	// face à un arbre ; -ErewhonTestFell : l'arbre tombe aussitôt ; -ErewhonTestLogs : un tas de 13 bûches et
	// une charrette de 6 bûches à côté, deux bûches en main.
	if (APawn* TestPawn = Controller->GetPawn(); TestPawn && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestForest")))
	{
		AErewhonGameState* TestState = GetGameState<AErewhonGameState>();
		USectorLayoutSubsystem* Layouts = GetWorld()->GetSubsystem<USectorLayoutSubsystem>();
		FIntPoint Woods(INT32_MAX, INT32_MAX);
		for (int32 Ring = 2; Ring < 8 && Woods.X == INT32_MAX; ++Ring)
		{
			for (int32 X = -Ring; X <= Ring && Woods.X == INT32_MAX; ++X)
			{
				for (int32 Y = -Ring; Y <= Ring && Woods.X == INT32_MAX; ++Y)
				{
					const FIntPoint Candidate(X, Y);
					const ESectorType Type = ErewhonSector::TypeAt(Candidate, TestState->GetWorldSeed());
					if ((Type == ESectorType::Edge || Type == ESectorType::Forest) && ErewhonSector::IsPurchasable(Candidate)
						&& Layouts->GetLayout(Candidate, TestState->GetWorldSeed())->Trees.Num() > 0)
					{
						Woods = Candidate;
					}
				}
			}
		}
		if (Woods.X != INT32_MAX)
		{
			TestState->SetSectorOwner(Woods, Key, DisplayName, 0, 0, 0);
			PlayerState->SetPlayerClass(EPlayerClass::Forester);
			if (FInventorySlot* Hand = Inventory->EditHandSlot())
			{
				Hand->ItemId = ErewhonForestry::Axe;
				Hand->Count = 1;
			}
			Inventory->CommitChanges();
			// L'arbre le plus près du centre du secteur, et le joueur à quatre mètres, face à lui.
			const TSharedRef<const FSectorLayout> Layout = Layouts->GetLayout(Woods, TestState->GetWorldSeed());
			int32 Best = 0;
			for (int32 i = 1; i < Layout->Trees.Num(); ++i)
			{
				if (FVector::Dist2D(Layout->Trees[i].Location, ErewhonSector::Center(Woods)) < FVector::Dist2D(Layout->Trees[Best].Location, ErewhonSector::Center(Woods)))
				{
					Best = i;
				}
			}
			const FVector TreeAt = Layout->Trees[Best].Location;
			const FVector2D Foot = FVector2D(TreeAt) - FVector2D(400.f, 0.f);
			TestPawn->TeleportTo(FVector(Foot, ErewhonTerrain::Height(Foot, TestState->GetWorldSeed()) + 100.f), FRotator::ZeroRotator);
			Controller->SetControlRotation(FRotator(-10.f, 0.f, 0.f));
			if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestFell")))
			{
				for (int32 Hit = 0; Hit < 12 && !TestState->IsHarvested(Woods, ErewhonForestry::HarvestIndexOfTree(Best)); ++Hit)
				{
					LastAxeHit.Reset();
					TryChopTree(Controller, Woods, Best);
				}
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestLogs")))
			{
				const FVector2D PileAt = Foot + FVector2D(150.f, -300.f);
				AStorageActor* Pile = SpawnStorage(EStorageKind::LogPile, FTransform(FRotator(0.f, 0.f, 0.f), FVector(PileAt, ErewhonTerrain::Height(PileAt, TestState->GetWorldSeed()))), Key, DisplayName);
				Pile->StoreItem(ErewhonForestry::Log, 13);
				Pile->Commit();
				const FVector2D CartAt2D = Foot + FVector2D(150.f, 300.f);
				const FTransform CartAt(FRotator(0.f, 0.f, 0.f), FVector(CartAt2D, ErewhonTerrain::Height(CartAt2D, TestState->GetWorldSeed())));
				ACart* LogCart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), CartAt);
				LogCart->Setup(Key, DisplayName);
				for (int32 i = 0; i < ACart::WoodSlots; ++i)
				{
					LogCart->EditWood()[i].ItemId = ErewhonForestry::Log;
					LogCart->EditWood()[i].Count = 2;
				}
				LogCart->FinishSpawning(CartAt);
				LogCart->CommitWood();
				if (FInventorySlot* Hand = Inventory->EditHandSlot())
				{
					Inventory->StoreItem(ErewhonForestry::Axe, 1);
					Hand->ItemId = ErewhonForestry::Log;
					Hand->Count = 2;
				}
			}
			Inventory->CommitChanges();
		}
	}
	// -ErewhonTestFarm : le secteur 1:1 au joueur avec ses quatre champs (-ErewhonTestFarmMerged : réunis), un peu
	// de chaque étape dans le champ nord-est (retourné, semé, pousses d'un et deux jours, mûr), et un fermier outillé.
	if (APawn* TestPawn = Controller->GetPawn(); TestPawn && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestFarm")))
	{
		using namespace ErewhonFarm;
		AErewhonGameState* TestState = GetGameState<AErewhonGameState>();
		const FIntPoint TestSector(1, 1);
		const uint8 TestFields = AllQuarters | FenceBit | (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestFarmMerged")) ? MergedBit : 0);
		TestState->SetSectorOwner(TestSector, Key, DisplayName, (1 << 1) | (1 << 3), 0, TestFields);
		PlayerState->SetPlayerClass(EPlayerClass::Farmer);
		for (const TPair<const TCHAR*, int32>& Gift : { TPair<const TCHAR*, int32>(TEXT("spade"), 1), { TEXT("scythe"), 1 },
			{ TEXT("seeds_wheat"), 5 }, { TEXT("seeds_flax"), 5 }, { TEXT("seeds_cabbage"), 5 } })
		{
			Inventory->StoreItem(Gift.Key, Gift.Value);
		}
		// -ErewhonTestDrop : du blé et du petit bois posés à terre devant le joueur (voir après la téléportation).
		// -ErewhonTestHotbar : la bêche, la faux et les graines de blé dans la barre (la bêche en main).
		if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestHotbar")))
		{
			const TPair<const TCHAR*, int32> Bar[] = { { TEXT("spade"), 1 }, { TEXT("scythe"), 1 }, { TEXT("seeds_wheat"), 5 } };
			for (int32 Hotbar = 0; Hotbar < UE_ARRAY_COUNT(Bar); ++Hotbar)
			{
				const int32 Index = Inventory->HotbarSlotIndex(Hotbar);
				if (Inventory->EditSlots().IsValidIndex(Index) && Inventory->RemoveStored(Bar[Hotbar].Key, Bar[Hotbar].Value))
				{
					Inventory->EditSlots()[Index].ItemId = Bar[Hotbar].Key;
					Inventory->EditSlots()[Index].Count = Bar[Hotbar].Value;
				}
			}
			int32 TestSlot = 0;
			FParse::Value(FCommandLine::Get(), TEXT("ErewhonTestHotbarSlot="), TestSlot);
			Inventory->SelectHotbar(TestSlot);
			Inventory->CommitChanges();
		}
		// Par bandes vers l'est : retourné, semé, pousses d'un jour, de deux, mûr ; blé, lin, chou vers le nord.
		const double Now = TestState->GetWorldClock();
		for (int32 Y = 26; Y < 44; ++Y)
		{
			const int32 Band = FMath::Min((Y - 26) / 3, 4);
			for (int32 X = 30; X < 47; ++X)
			{
				const uint8 Crop = Band == 0 ? 0 : static_cast<uint8>(1 + FMath::Min((X - 30) / 6, 2));
				TestState->SetFieldCell(TestSector, Y * CellsPerSide + X, Crop, Now - DaySeconds() * FMath::Max(0, Band - 1) - 5.0);
			}
		}
		FString FarmMenu;
		const bool bFarmAtManager = FParse::Value(FCommandLine::Get(), TEXT("ErewhonOpenMenu="), FarmMenu) && (FarmMenu == TEXT("sectors") || FarmMenu == TEXT("store"));
		const FVector2D Foot = FVector2D(ErewhonSector::Origin(TestSector)) + FVector2D(7600.f, 4980.f);
		if (!bFarmAtManager)   // pour la mairie ou la boutique, on reste au village
		TestPawn->TeleportTo(FVector(Foot, ErewhonTerrain::Height(Foot, TestState->GetWorldSeed()) + 100.f), FRotator(0.f, 90.f, 0.f));
		if (!bFarmAtManager)
		{
			Controller->SetControlRotation(FRotator(-15.f, 90.f, 0.f));
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestDrop")))
		{
			DropOnGround(TestPawn, TEXT("wheat"), 12);
			TestPawn->AddActorWorldOffset(FVector(0.f, -150.f, 0.f));
			DropOnGround(TestPawn, TEXT("firewood"), 3);
			TestPawn->AddActorWorldOffset(FVector(0.f, -120.f, 0.f));
			Inventory->StoreItem(TEXT("firewood"), 8);   // de quoi peser lourd : 20 kg de petit bois
		}
		// -ErewhonTestTow=cultivator|seeder : en selle sur un bhurak, l'outil attelé derrière, levier baissé
		// (le semoir garni de graines de blé), au bord du champ sud-est, face à l'est.
		FString TowName;
		if (FParse::Value(FCommandLine::Get(), TEXT("ErewhonTestTow="), TowName))
		{
			const ECartKind TowKind = TowName == TEXT("seeder") ? ECartKind::Seeder : TowName == TEXT("harvester") ? ECartKind::Harvester : ECartKind::Cultivator;
			const FVector2D Start = FVector2D(ErewhonSector::Origin(TestSector)) + FVector2D(2500.f, 5600.f);
			const FTransform BeastAt(FRotator(0.f, 90.f, 0.f), FVector(Start, ErewhonTerrain::Height(Start, TestState->GetWorldSeed())));
			ABhurak* Beast = SpawnBhurak(BeastAt, Key, DisplayName);
			Beast->SetHarness(true);
			Beast->SetGear(ABhurak::BackSlot, ABhurak::SaddleItem, nullptr);
			const FVector2D Behind = Start - FVector2D(0.f, 300.f);
			const FTransform ToolAt(FRotator(0.f, 90.f, 0.f), FVector(Behind, ErewhonTerrain::Height(Behind, TestState->GetWorldSeed())));
			ACart* Tool = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), ToolAt);
			Tool->Setup(Key, DisplayName, TowKind);
			if (TowKind == ECartKind::Seeder)
			{
				Tool->EditWood()[0].ItemId = TEXT("seeds_wheat");
				Tool->EditWood()[0].Count = 10;
			}
			Tool->FinishSpawning(ToolAt);
			Tool->CommitWood();
			TestPawn->TeleportTo(BeastAt.GetLocation() + FVector(0.f, 0.f, 100.f), BeastAt.Rotator());
			TryRideBhurak(Controller, Beast);
			Tool->StartDriving(Beast);
			Beast->SetTowedCart(Tool);
			Tool->SetEngaged(true);
			// Le semoir passe sur de la terre retournée : une bande retournée vers l'est.
			if (TowKind == ECartKind::Seeder)
			{
				for (int32 Y = 28; Y < 46; ++Y)
				{
					for (int32 X = 10; X < 15; ++X)
					{
						TestState->SetFieldCell(TestSector, Y * CellsPerSide + X, 0, Now);
					}
				}
			}
		}
		else
		{
			PlayerState->SetCoins(9000);
		}
	}
	// -ErewhonTestBhurak : un bhurak harnaché et sellé, deux sacoches (l'une garnie), une charrette derrière lui ;
	// -ErewhonTestRide : le joueur en selle, la charrette attelée.
	if (APawn* TestPawn = Controller->GetPawn(); TestPawn && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestBhurak")))
	{
		const AErewhonGameState* TestState = GetGameState<AErewhonGameState>();
		const FVector2D Foot = FVector2D(TestPawn->GetActorLocation()) + FVector2D(450.f, 0.f);
		const FTransform BeastAt(FRotator(0.f, 90.f, 0.f), FVector(Foot, ErewhonTerrain::Height(Foot, TestState->GetWorldSeed())));
		ABhurak* Beast = SpawnBhurak(BeastAt, Key, DisplayName);
		Beast->SetHarness(true);
		Beast->SetGear(ABhurak::BackSlot, ABhurak::SaddleItem, nullptr);
		for (const int32 Slot : { 0, 2 })
		{
			AStorageActor* Bag = SpawnStorage(EStorageKind::Saddlebag, Beast->GearTransform(Slot) * BeastAt, Key, DisplayName);
			if (Slot == 0)
			{
				Bag->StoreItem(TEXT("wheat"), 20);
				Bag->StoreItem(TEXT("firewood"), 6);
			}
			Beast->SetGear(Slot, ABhurak::SaddlebagItem, Bag);
		}
		Inventory->StoreItem(TEXT("saddlebag"), 1);
		const FVector2D CartFoot = Foot - FVector2D(0.f, 380.f);
		const FTransform CartAt(FRotator(0.f, 90.f, 0.f), FVector(CartFoot, ErewhonTerrain::Height(CartFoot, TestState->GetWorldSeed())));
		ACart* TestCart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), CartAt);
		TestCart->Setup(Key, DisplayName);
		TestCart->FinishSpawning(CartAt);
		if (AErewhonCharacter* Rider = Cast<AErewhonCharacter>(TestPawn); Rider && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestRide")))
		{
			TryRideBhurak(Controller, Beast);
			TestCart->StartDriving(Beast);
			Beast->SetTowedCart(TestCart);
		}
	}
	// -ErewhonTestApothecary : le secteur 1:1 au joueur, avec ses chemins et son apothicairerie (feu allumé),
	// de quoi y travailler ; le joueur entre dans la maison.
	if (APawn* TestPawn = Controller->GetPawn(); TestPawn && FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestApothecary")))
	{
		AErewhonGameState* TestState = GetGameState<AErewhonGameState>();
		const FIntPoint TestSector(1, 1);
		// -ErewhonTestStoreroom : avec sa remise.
		const uint8 TestBuildings = ESectorBuilding::Apothecary | (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestStoreroom")) ? ESectorBuilding::Storeroom : 0);
		TestState->SetSectorOwner(TestSector, Key, DisplayName, (1 << 1) | (1 << 3), 0, TestBuildings);
		TArray<FInventorySlot> TestShelves;
		TestShelves.SetNum(12);
		const TPair<const TCHAR*, int32> Stocked[] = { { TEXT("potion_speed"), 10 }, { TEXT("crushed_flower_purple"), 6 }, { TEXT("potion_night"), 7 },
			{ TEXT("potion_grow"), 3 }, { TEXT("potion_jump"), 10 } };
		const int32 Where[] = { 0, 1, 3, 6, 9 };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Stocked); ++i)
		{
			TestShelves[Where[i]].ItemId = Stocked[i].Key;
			TestShelves[Where[i]].Count = Stocked[i].Value;
		}
		if (!Apothecaries.Contains(TestSector))
		{
			SpawnApothecary(TestSector, TestState->GetWorldClock() + 600.0);
			if (!FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestEmpty")))
			{
				SpawnFurnishing(TestSector, Key, DisplayName, TestShelves);
				// Un séchoir garni (un crochet déjà sec), un atelier, un pot-pourri allumé près de la porte.
				const FTransform HouseT = ErewhonApothecary::HouseTransform(TestSector, TestState->GetWorldSeed());
				const float Floor = ErewhonSpawn::CabinFloorTop;
				AStorageActor* Rack = SpawnStorage(EStorageKind::DryingRack, FTransform(FRotator(0.f, 0.f, 0.f), FVector(-330.f, -220.f, Floor)) * HouseT, Key, DisplayName);
				Rack->StoreItem(TEXT("flower_blue"), 8);
				Rack->StoreItem(TEXT("herb"), 3);
				Rack->EditSlots()[2].ItemId = TEXT("dried_flower_purple");
				Rack->EditSlots()[2].Count = 6;
				Rack->Commit();
				SpawnStorage(EStorageKind::Workbench, FTransform(FRotator(0.f, -90.f, 0.f), FVector(150.f, -290.f, Floor)) * HouseT, Key, DisplayName);
				AStorageActor* Pot = SpawnStorage(EStorageKind::Potpourri, FTransform(FVector(-560.f, 160.f, ErewhonTerrain::Height(FVector2D(HouseT.TransformPosition(FVector(-560.f, 160.f, 0.f))), TestState->GetWorldSeed()) - HouseT.GetLocation().Z)) * HouseT, Key, DisplayName);
				Pot->SetExpiresAt(TestState->GetWorldClock() + ErewhonPlaceables::PotpourriSeconds);
			}
		}
		// De quoi brasser un breuvage de vitesse (4 L : 20 écrasés de fleur bleue, 8 d'herbe), et l'outillage.
		PlayerState->SetPlayerClass(EPlayerClass::Apothecary);
		for (const TPair<const TCHAR*, int32>& Gift : { TPair<const TCHAR*, int32>(TEXT("firewood"), 3), { TEXT("crushed_flower_blue"), 20 },
			{ TEXT("crushed_grass"), 8 }, { TEXT("pitcher"), 2 }, { TEXT("vial"), 10 }, { TEXT("herbal_book"), 1 }, { TEXT("potion_speed"), 1 } })
		{
			Inventory->StoreItem(Gift.Key, Gift.Value);
		}
		if (const TOptional<FTransform> Spot = ErewhonApothecary::StandSpot(this, TestSector))
		{
			// Un tonneau d'eau à côté de la cheminée.
			AStorageActor* WaterBarrel = SpawnStorage(EStorageKind::Barrel,
				FTransform(FVector(-90.f, -210.f, 0.f)) * FTransform(Spot->GetRotation(), Spot->GetLocation()), Key, DisplayName);
			WaterBarrel->SetLiquid(ErewhonApothecary::Water, 60.f);
			// -ErewhonTestPitcher : le pichet en main, on puise 2 L au tonneau, puis on en verse au chaudron en s'arrêtant à 1,5 s.
			if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestPitcher")))
			{
				if (FInventorySlot* Hand = Inventory->EditHandSlot())
				{
					Inventory->RemoveStored(ErewhonApothecary::EmptyPitcher, 1);
					Hand->ItemId = ErewhonApothecary::EmptyPitcher;
					Hand->Count = 1;
					Inventory->CommitChanges();
				}
				TWeakObjectPtr<AErewhonPlayerController> WeakPC = Controller;
				TWeakObjectPtr<AStorageActor> WeakBarrel = WaterBarrel;
				const auto Report = [WeakPC, WeakBarrel, this](const TCHAR* When)
				{
					AStorageActor* Cauldron = nullptr;
					for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It) { if (It->GetKind() == EStorageKind::Cauldron) { Cauldron = *It; } }
					if (WeakPC.IsValid() && WeakBarrel.IsValid())
					{
						UE_LOG(LogTemp, Warning, TEXT("PITCHER %s hand=%s barrel=%.1f cauldron=%.1f"), When, *WeakPC->GetPlayerState<AErewhonPlayerState>()->GetInventory()->GetHandItem().ToString(),
							WeakBarrel->GetWater(), Cauldron ? Cauldron->GetWater() : -1.f);
					}
				};
				FTimerHandle T1, T2, T3, T4;
				GetWorldTimerManager().SetTimer(T1, FTimerDelegate::CreateWeakLambda(this, [this, WeakPC, WeakBarrel, Report]() { if (APawn* Near = WeakPC.IsValid() ? WeakPC->GetPawn() : nullptr) { Near->TeleportTo(WeakBarrel->GetActorLocation() + FVector(0.f, 0.f, 160.f), Near->GetActorRotation()); } Report(TEXT("start")); UE_LOG(LogTemp, Warning, TEXT("PITCHER draw: %s"), *TryStartPour(WeakPC.Get(), WeakBarrel.Get(), 5, NAME_None, 1).ToString()); }), 8.f, false);
				GetWorldTimerManager().SetTimer(T2, FTimerDelegate::CreateWeakLambda(this, [this, WeakPC, Report]()
				{
					Report(TEXT("after draw"));
					AStorageActor* Cauldron = nullptr;
					for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It) { if (It->GetKind() == EStorageKind::Cauldron) { Cauldron = *It; } }
					UE_LOG(LogTemp, Warning, TEXT("PITCHER pour: %s"), *TryStartPour(WeakPC.Get(), Cauldron, 6, NAME_None, 1).ToString());
				}), 11.f, false);
				GetWorldTimerManager().SetTimer(T3, FTimerDelegate::CreateWeakLambda(this, [WeakPC]() { if (WeakPC.IsValid()) { WeakPC->ServerStopCraft(); } }), 12.5f, false);
				GetWorldTimerManager().SetTimer(T4, FTimerDelegate::CreateWeakLambda(this, [Report]() { Report(TEXT("after stopped pour")); }), 14.f, false);
			}
			// Un rack à petit bois garni, de l'autre côté de la cheminée, et une charrette de bois devant la porte.
			AStorageActor* WoodRack = SpawnStorage(EStorageKind::FirewoodRack,
				FTransform(FRotator(0.f, 180.f, 0.f), FVector(-70.f, 230.f, 0.f)) * FTransform(Spot->GetRotation(), Spot->GetLocation()), Key, DisplayName);
			WoodRack->StoreItem(TEXT("firewood"), 25);
			const FTransform HouseT = ErewhonApothecary::HouseTransform(TestSector, TestState->GetWorldSeed());
			const FVector CartFoot = HouseT.TransformPosition(FVector(-1000.f, 300.f, 0.f));
			const FTransform CartAt(HouseT.Rotator() + FRotator(0.f, 90.f, 0.f), FVector(CartFoot.X, CartFoot.Y, ErewhonTerrain::Height(FVector2D(CartFoot), TestState->GetWorldSeed())));
			ACart* WoodCart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), CartAt);
			WoodCart->Setup(Key, DisplayName);
			WoodCart->EditWood()[0].ItemId = TEXT("firewood");
			WoodCart->EditWood()[0].Count = 10;
			WoodCart->EditWood()[1].ItemId = TEXT("firewood");
			WoodCart->EditWood()[1].Count = 8;
			WoodCart->FinishSpawning(CartAt);
			WoodCart->CommitWood();
			// -ErewhonTestBrew : le chaudron déjà prêt pour un breuvage de vitesse (4 L, au cran qui vise 65 °C).
			if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestBrew")))
			{
				if (AStorageActor* Stand = ErewhonApothecary::FindStand(this, TestSector))
				{
					for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It)
					{
						if (It->GetKind() == EStorageKind::Cauldron)
						{
							Stand->SetLevel(0.75f);
							It->SetActorLocation(ErewhonApothecary::CauldronSpotOn(Stand).GetLocation());
							It->SetLiquid(ErewhonApothecary::BaseLiquid, 4.f);   // les potions se brassent dans la base
							It->StoreItem(TEXT("crushed_flower_blue"), 20);
							It->StoreItem(TEXT("crushed_grass"), 8);
							// -ErewhonTestVial : une fois le breuvage prêt, une fiole en main se remplit au chaudron.
							if (FParse::Param(FCommandLine::Get(), TEXT("ErewhonTestVial")))
							{
								TWeakObjectPtr<AErewhonPlayerController> WeakPC = Controller;
								TWeakObjectPtr<AStorageActor> WeakPot = *It;
								FTimerHandle VialTimer, VialReport;
								GetWorldTimerManager().SetTimer(VialTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakPC, WeakPot]()
								{
									if (!WeakPC.IsValid() || !WeakPot.IsValid()) { return; }
									UInventoryComponent* Inv = WeakPC->GetPlayerState<AErewhonPlayerState>()->GetInventory();
									Inv->RemoveStored(ErewhonApothecary::EmptyVial, 2);
									FInventorySlot* Hand = Inv->EditHandSlot();
									Hand->ItemId = ErewhonApothecary::EmptyVial;
									Hand->Count = 2;
									Inv->CommitChanges();
									if (APawn* Near = WeakPC->GetPawn()) { Near->TeleportTo(WeakPot->GetActorLocation() + FVector(-120.f, 0.f, 100.f), Near->GetActorRotation()); }
									UE_LOG(LogTemp, Warning, TEXT("VIAL liquid=%s %.2f L, start: %s"), *WeakPot->GetLiquid().ToString(), WeakPot->GetWater(), *TryStartPour(WeakPC.Get(), WeakPot.Get(), 2, NAME_None, 1).ToString());
								}), 95.f, false);
								GetWorldTimerManager().SetTimer(VialReport, FTimerDelegate::CreateWeakLambda(this, [WeakPC, WeakPot]()
								{
									if (!WeakPC.IsValid() || !WeakPot.IsValid()) { return; }
									const UInventoryComponent* Inv = WeakPC->GetPlayerState<AErewhonPlayerState>()->GetInventory();
									UE_LOG(LogTemp, Warning, TEXT("VIAL after: hand=%s x? cauldron %.2f L, speed potions stored=%d"), *Inv->GetHandItem().ToString(), WeakPot->GetWater(), Inv->CountStored(TEXT("potion_speed")));
								}), 97.f, false);
							}
						}
					}
				}
			}
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

FText AErewhonGameMode::TryHarvest(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 PlantIndex, bool bNow)
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
	// On cueille d'abord (une seconde à la main, une demi à la cerpe), on ramasse au bout.
	if (!bNow)
	{
		const bool bSickle = !Hand.IsNone();
		StartTimedGesture(Controller, bSickle ? ErewhonFarm::SickleHarvestSeconds : ErewhonFarm::HandHarvestSeconds, bSickle ? 5 : 4,
			[this, Sector, PlantIndex](AErewhonPlayerController* Picker) { return TryHarvest(Picker, Sector, PlantIndex, true); });
		return FText::GetEmpty();
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
	// Le commerce du village où se tient le joueur (celui dont la place est la plus proche).
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), ErewhonSpawn::MerchantLocation(PlayerVillage(Controller), Kind))
		<= UErewhonSettings::Get().MerchantReach() + 200.f;
}

FText AErewhonGameMode::TryBuySector(AErewhonPlayerController* Controller, const FIntPoint& Sector)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
	}
	// Seule la mairie du village du secteur le vend, relié à son territoire (voir AErewhonGameState::WhyNotBuyable).
	const FIntPoint Village = ErewhonVillages::NearestVillage(ErewhonSector::FromWorld(Controller->GetPawn()->GetActorLocation()));
	const FText Why = State->WhyNotBuyable(Sector, Village, PlayerState);
	if (!Why.IsEmpty())
	{
		return Why;
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
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
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

AApothecary* AErewhonGameMode::SpawnApothecary(const FIntPoint& Sector, double FireUntil)
{
	if (const TObjectPtr<AApothecary>* Existing = Apothecaries.Find(Sector); Existing && *Existing)
	{
		return *Existing;
	}
	const FTransform At = ErewhonApothecary::HouseTransform(Sector, GetGameState<AErewhonGameState>()->GetWorldSeed());
	AApothecary* House = GetWorld()->SpawnActorDeferred<AApothecary>(AApothecary::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	House->Setup(Sector, FireUntil);
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
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey())
	{
		return LOCTEXT("NotYours", "Ce secteur ne vous appartient pas.");
	}
	if (!ErewhonSector::IsPurchasable(Sector))
	{
		return LOCTEXT("NotPrairie", "L'apothicairerie se construit sur un secteur à vous.");
	}
	if (State->GetBuildings(Sector) & ESectorBuilding::Apothecary)
	{
		return LOCTEXT("AlreadyBuilt", "L'apothicairerie est déjà construite.");
	}
	if (State->GetBuildings(Sector) & ErewhonFarm::AnyField)
	{
		return LOCTEXT("HouseOnFields", "Des champs occupent ce secteur : l'apothicairerie n'y a pas sa place.");
	}
	if (const FText NotOne = CheckApothecary(Controller); !NotOne.IsEmpty())
	{
		return NotOne;
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

FText AErewhonGameMode::CheckAtStation(AErewhonPlayerController* Controller, const AStorageActor* Station) const
{
	if (const FText NotOne = CheckApothecary(Controller); !NotOne.IsEmpty())
	{
		return NotOne;
	}
	const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !PlayerState->IsRegistered() || !Pawn || !Station
		|| (Station->GetKind() != EStorageKind::Mortar && Station->GetKind() != EStorageKind::Cauldron && Station->GetKind() != EStorageKind::Workbench))
	{
		return LOCTEXT("NoStation", "Il n'y a pas de quoi préparer ici.");
	}
	if (!Station->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("NotYourStation", "Ce n'est pas à vous.");
	}
	if (Station->GetCarrier() || FVector::Dist(Pawn->GetActorLocation(), Station->GetActorLocation()) > ErewhonApothecary::UseReach)
	{
		return LOCTEXT("StationTooFar", "Vous êtes trop loin.");
	}
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
	if (FVector::Dist2D(Pawn->GetActorLocation(), House->GetActorLocation()) > 650.f)
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

	/**
	 * Une fois les ingrédients pris (poches d'abord, puis sac), le résultat aurait-il sa place ?
	 * On le simule sur une copie des poches et du sac.
	 */
	bool FitsAfterCraft(const AErewhonPlayerState* PlayerState, const ErewhonApothecary::FRecipe& Recipe, const UItemDatabase& Items)
	{
		TArray<FInventorySlot> Pockets = PlayerState->GetInventory()->GetSlots();
		Pockets.RemoveAll([](const FInventorySlot& S) { return S.Kind != ESlotKind::Inventory; });
		const UBackpackComponent* Bag = PlayerState->GetBackpack();
		TArray<FInventorySlot> BagSlots = Bag && Bag->IsOwned() ? Bag->GetSlots() : TArray<FInventorySlot>();
		BagSlots.RemoveAll([](const FInventorySlot& S) { return S.Kind != ESlotKind::Inventory; });
		const auto Take = [](TArray<FInventorySlot>& Slots, FName Item, int32& Left)
		{
			for (FInventorySlot& Slot : Slots)
			{
				if (Left > 0 && !Slot.IsEmpty() && Slot.ItemId == Item)
				{
					const int32 Taken = FMath::Min(Left, Slot.Count);
					Slot.Count -= Taken;
					Left -= Taken;
					if (Slot.Count <= 0)
					{
						Slot.Clear();
					}
				}
			}
		};
		for (const TPair<FName, int32>& Input : Recipe.Inputs)
		{
			int32 Left = Input.Value;
			Take(Pockets, Input.Key, Left);
			Take(BagSlots, Input.Key, Left);
		}
		const FItemDef* Output = Items.Find(Recipe.Output);
		const int32 MaxStack = Output ? Output->MaxStack : 1;
		const auto HasRoom = [&Recipe, MaxStack](const TArray<FInventorySlot>& Slots)
		{
			return Slots.ContainsByPredicate([&Recipe, MaxStack](const FInventorySlot& S) { return S.IsEmpty() || (S.ItemId == Recipe.Output && S.Count < MaxStack); });
		};
		return HasRoom(Pockets) || HasRoom(BagSlots);
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
	if (const FText NotOne = CheckApothecary(Controller); !NotOne.IsEmpty())
	{
		return NotOne;
	}
	const FText Error = CheckAtHouse(Controller, House);
	if (!Error.IsEmpty())
	{
		return Error;
	}
	if (House->FireRemaining() > ErewhonApothecary::MaxFireSeconds - ErewhonApothecary::FireSecondsPerWood)
	{
		return LOCTEXT("FireFull", "Le feu est déjà bien nourri.");
	}
	// Son petit bois, ou celui du rack posé près de la cheminée.
	if (!TakeOwned(Controller->GetPlayerState<AErewhonPlayerState>(), TEXT("firewood"), 1) && !ErewhonApothecary::TakeWoodNearFire(House))
	{
		return LOCTEXT("NoFirewood", "Il vous faut du petit bois (au General Store), ou un rack garni près de la cheminée.");
	}
	House->AddFire(ErewhonApothecary::FireSecondsPerWood);
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryStartCraft(AErewhonPlayerController* Controller, AStorageActor* Station, FName Output, int32 Count)
{
	const FText Error = CheckAtStation(Controller, Station);
	if (!Error.IsEmpty())
	{
		return Error;
	}
	const TOptional<ErewhonApothecary::FRecipe> Recipe = ErewhonApothecary::FindRecipe(*UItemDatabase::Get(this), Output);
	const ErewhonApothecary::ECraft StationCraft = Station->GetKind() == EStorageKind::Cauldron ? ErewhonApothecary::ECraft::Cauldron
		: Station->GetKind() == EStorageKind::Workbench ? ErewhonApothecary::ECraft::Workbench : ErewhonApothecary::ECraft::Mortar;
	if (!Recipe.IsSet() || Count <= 0 || Recipe->Craft != StationCraft)
	{
		return LOCTEXT("NoRecipe", "Je ne sais pas préparer cela.");
	}
	if (Recipe->Craft == ErewhonApothecary::ECraft::Cauldron)
	{
		const AApothecary* House = ErewhonApothecary::HouseOfHungCauldron(Station);
		if (!House)
		{
			return LOCTEXT("NotHung", "Le chaudron doit pendre à la crémaillère d'une cheminée.");
		}
		if (!House->IsFireLit())
		{
			return LOCTEXT("FireOut", "Le feu est éteint : allumez-le avec du petit bois.");
		}
		if (Station->GetWater() < ErewhonPlaceables::WaterPerBrew)
		{
			return LOCTEXT("CauldronDry", "Le chaudron est vide : remplissez-le d'eau (un tonneau d'eau à quelques pas).");
		}
	}
	if (!FitsAfterCraft(Controller->GetPlayerState<AErewhonPlayerState>(), *Recipe, *UItemDatabase::Get(this)))
	{
		return LOCTEXT("NoRoomToCraft", "Plus de place dans l'inventaire pour ce que vous préparez : faites d'abord de la place.");
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
	Controller->SetCraftJob(Job, Station);
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
	AStorageActor* Station = Controller->GetCraftStation();
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
	const FText Error = CheckAtStation(Controller, Station);
	if (!Error.IsEmpty())
	{
		Stop(Error);
		return;
	}
	const bool bCauldron = Recipe->Craft == ErewhonApothecary::ECraft::Cauldron;
	const AApothecary* House = bCauldron ? ErewhonApothecary::HouseOfHungCauldron(Station) : nullptr;
	if (bCauldron && Station->GetWater() < ErewhonPlaceables::WaterPerBrew)
	{
		Stop(LOCTEXT("CauldronRanDry", "Le chaudron n'a plus d'eau."));
		return;
	}
	if (bCauldron && (!House || !House->IsFireLit()))
	{
		Stop(LOCTEXT("FireWentOut", "Le feu s'est éteint : le chaudron refroidit."));
		return;
	}
	if (!HasIngredients(PlayerState, *Recipe))
	{
		Stop(LOCTEXT("OutOfIngredients", "Plus assez d'ingrédients."));
		return;
	}
	if (!FitsAfterCraft(PlayerState, *Recipe, *UItemDatabase::Get(this)))
	{
		Stop(LOCTEXT("NoRoom", "Plus de place dans l'inventaire."));
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
	if (bCauldron)
	{
		Station->SetWater(Station->GetWater() - ErewhonPlaceables::WaterPerBrew);
	}
	Controller->ClientCraftStep(static_cast<uint8>(Recipe->Craft));

	// Le suivant, s'il en reste à faire et de quoi le faire.
	--Job.Remaining;
	if (Job.Remaining <= 0 || !HasIngredients(PlayerState, *Recipe))
	{
		Stop(FText::GetEmpty());
		return;
	}
	Job.StepEnd = GetGameState<AErewhonGameState>()->GetWorldClock() + Job.StepSeconds;
	Controller->SetCraftJob(Job, Station);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	GetWorldTimerManager().SetTimer(Controller->CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
	{
		if (WeakController.IsValid())
		{
			StepCraft(WeakController.Get());
		}
	}), Job.StepSeconds, false);
}

void AErewhonGameMode::FillAtFountain()
{
	const float Step = ErewhonVillage::FountainLitersPerSecond * 0.25f;
	for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It)
	{
		// Sous le bec de la fontaine de son village (le plus proche).
		AStorageActor* Barrel = *It;
		if (Barrel->GetKind() != EStorageKind::Barrel || Barrel->GetCarrier()
			|| FVector::Dist2D(Barrel->GetActorLocation(), ErewhonVillage::FountainBarrelSpot(ErewhonSpawn::VillageAt(Barrel->GetActorLocation()))) > 80.f
			|| Barrel->GetWater() >= ErewhonPlaceables::WaterBarrelLiters || (Barrel->GetWater() > 0.01f && !Barrel->IsWaterBarrel())
			|| Barrel->GetSlots().ContainsByPredicate([](const FInventorySlot& Slot) { return !Slot.IsEmpty(); }))
		{
			continue;
		}
		// Le propriétaire paie chaque litre commencé : 1 C le litre. Absent ou sans le sou, l'eau ne coule plus.
		AErewhonPlayerState* Payer = nullptr;
		for (APlayerState* PS : GameState->PlayerArray)
		{
			AErewhonPlayerState* Candidate = Cast<AErewhonPlayerState>(PS);
			if (Candidate && Barrel->IsOwnedBy(Candidate->GetPlayerKey()))
			{
				Payer = Candidate;
			}
		}
		const float Before = Barrel->GetWater();
		const float After = FMath::Min(Before + Step, ErewhonPlaceables::WaterBarrelLiters);
		const int32 Cost = FMath::CeilToInt32(After) - FMath::CeilToInt32(Before);
		if (!Payer || (Cost > 0 && !Payer->SpendCoins(Cost)))
		{
			if (Payer && !BrokeAtFountain.Contains(Barrel))
			{
				BrokeAtFountain.Add(Barrel);
				if (AErewhonPlayerController* PC = Cast<AErewhonPlayerController>(Payer->GetOwningController()))
				{
					PC->ClientToast(LOCTEXT("FountainBroke", "Plus assez de pièces pour l'eau de la fontaine (1 C le litre)."));
				}
			}
			continue;
		}
		BrokeAtFountain.Remove(Barrel);
		Barrel->SetLiquid(ErewhonApothecary::Water, After);
	}
}

FText AErewhonGameMode::CheckApothecary(AErewhonPlayerController* Controller) const
{
	const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	return PlayerState && PlayerState->IsApothecary() ? FText::GetEmpty()
		: LOCTEXT("NotApothecary", "Réservé aux apothicaires : choisissez cette classe à la mairie.");
}

FText AErewhonGameMode::TryChooseClass(AErewhonPlayerController* Controller, uint8 NewClass)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
	}
	const EPlayerClass Wanted = NewClass <= static_cast<uint8>(EPlayerClass::Forester) ? static_cast<EPlayerClass>(NewClass) : EPlayerClass::None;
	if (PlayerState->GetPlayerClass() == Wanted)
	{
		return FText::GetEmpty();
	}
	const int32 Price = Wanted == EPlayerClass::Apothecary ? UErewhonSettings::Get().ApothecaryClassPrice
		: Wanted == EPlayerClass::Farmer ? UErewhonSettings::Get().FarmerClassPrice
		: Wanted == EPlayerClass::Forester ? UErewhonSettings::Get().ForesterClassPrice : 0;
	if (Price > 0 && !PlayerState->SpendCoins(Price))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	PlayerState->SetPlayerClass(Wanted);
	Controller->ClientToast(Wanted == EPlayerClass::Apothecary ? LOCTEXT("NowApothecary", "Vous voilà apothicaire !")
		: Wanted == EPlayerClass::Farmer ? LOCTEXT("NowFarmer", "Vous voilà fermier !")
		: Wanted == EPlayerClass::Forester ? LOCTEXT("NowForester", "Vous voilà forestier !")
		: LOCTEXT("NowNone", "Vous n'avez plus de classe particulière."));
	return FText::GetEmpty();
}

namespace
{
	/** Le joueur est à portée de ce récipient (le sien), et il ne le porte pas. */
	bool AtContainer(const AErewhonPlayerController* Controller, const AStorageActor* Container)
	{
		const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
		const APawn* Pawn = Controller->GetPawn();
		return PlayerState && Pawn && Container && Container->IsOwnedBy(PlayerState->GetPlayerKey()) && !Container->GetCarrier()
			&& (Container->GetKind() == EStorageKind::Barrel || Container->GetKind() == EStorageKind::Cauldron)
			&& FVector::Dist(Pawn->GetActorLocation(), Container->GetActorLocation()) <= 450.f;
	}

}

FText AErewhonGameMode::TryStartPour(AErewhonPlayerController* Controller, AStorageActor* Container, uint8 Op, FName Item, int32 Count)
{
	if (!AtContainer(Controller, Container) || Count <= 0 || (Op != 2 && Op != 5 && Op != 6))
	{
		return LOCTEXT("ContainerTooFar", "Vous êtes trop loin.");
	}
	if (Container->GetKind() == EStorageKind::Cauldron)
	{
		const FText Error = CheckApothecary(Controller);
		if (!Error.IsEmpty())
		{
			return Error;
		}
	}
	using namespace ErewhonApothecary;
	// L'objet qu'affiche la barre de progression, et la durée d'une étape (2 L/s).
	FName Shown = Item;
	float Seconds = PitcherLiters / PourRate;
	switch (Op)
	{
	case 5:
	case 6:
	{
		// Le pichet en main : on y puise (5) ou on en verse (6), un litre par seconde.
		const FName Hand = Controller->GetPlayerState<AErewhonPlayerState>()->GetInventory()->GetHandItem();
		const int32 InPitcher = PitcherLitersOf(Hand);
		const FName Held = LiquidOfPitcher(Hand);
		if (InPitcher < 0)
		{
			return LOCTEXT("NoPitcherInHand", "Prenez un pichet en main.");
		}
		if (Op == 5)
		{
			if (InPitcher >= 2)
			{
				return LOCTEXT("PitcherFull", "Le pichet est plein.");
			}
			if (Container->GetWater() < 1.f - 0.001f)
			{
				return LOCTEXT("NotALiter", "Il n'y a pas un litre à puiser.");
			}
			if (InPitcher > 0 && Container->GetLiquid() != Held)
			{
				return LOCTEXT("PitcherOther", "Le pichet contient un autre liquide : videz-le d'abord.");
			}
			Count = FMath::Min(2 - InPitcher, FMath::FloorToInt32(Container->GetWater() + 0.001f));
			Shown = PitcherFor(Container->GetLiquid(), InPitcher + 1);
		}
		else
		{
			if (InPitcher == 0)
			{
				return LOCTEXT("PitcherEmpty", "Le pichet est vide.");
			}
			if (Container->GetWater() > 0.01f && Container->GetLiquid() != Held)
			{
				return LOCTEXT("OtherLiquid", "Il contient déjà un autre liquide.");
			}
			if (Container->GetWater() + 1.f > Container->GetCapacityLiters() + 0.001f)
			{
				return LOCTEXT("Full", "C'est plein.");
			}
			if (Container->GetKind() == EStorageKind::Barrel && Container->GetSlots().ContainsByPredicate([](const FInventorySlot& S) { return !S.IsEmpty(); }))
			{
				return LOCTEXT("BarrelHasPlants", "Ce tonneau contient des plantes.");
			}
			Count = FMath::Min(InPitcher, FMath::FloorToInt32(Container->GetCapacityLiters() - Container->GetWater() + 0.001f));
			Shown = Hand;
		}
		Seconds = PitcherSecondsPerLiter;
		break;
	}
	case 2:
	{
		const FBrew* Brew = FindBrew(Container->GetLiquid());
		if (!Brew || Container->GetWater() < VialLiters)
		{
			return LOCTEXT("NoBrew", "Il n'y a pas de breuvage à mettre en fiole.");
		}
		Shown = Brew->Potion;
		Seconds = VialLiters / PourRate;
		break;
	}
	default:
		return FText::GetEmpty();
	}
	PourOps.Add(Controller, { Op, Container, Item });
	FErewhonCraftJob Job;
	Job.Output = Shown;
	Job.Pour = Op == 5 ? 1 : Op == 6 ? 2 : 0;
	Job.Remaining = FMath::Min(Count, 999);
	Job.StepSeconds = Seconds;
	Job.StepEnd = GetGameState<AErewhonGameState>()->GetWorldClock() + Seconds;
	Controller->SetCraftJob(Job, Container);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	GetWorldTimerManager().SetTimer(Controller->CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
	{
		if (WeakController.IsValid())
		{
			StepPour(WeakController.Get());
		}
	}), Seconds, false);
	return FText::GetEmpty();
}

void AErewhonGameMode::StepPour(AErewhonPlayerController* Controller)
{
	using namespace ErewhonApothecary;
	FErewhonCraftJob Job = Controller->GetCraftJob();
	const FPourOp* Found = PourOps.Find(Controller);
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const auto Stop = [this, Controller](const FText& Why)
	{
		PourOps.Remove(Controller);
		Controller->SetCraftJob(FErewhonCraftJob(), nullptr);
		if (!Why.IsEmpty())
		{
			Controller->ClientToast(Why);
		}
	};
	AStorageActor* Container = Found ? Found->Container.Get() : nullptr;
	if (!Found || !Job.IsActive() || !PlayerState || !AtContainer(Controller, Container))
	{
		Stop(FText::GetEmpty());
		return;
	}
	const FPourOp Op = *Found;
	const float Capacity = Container->GetCapacityLiters();
	FText Why;
	switch (Op.Op)
	{
	case 5:   // puiser un litre
	case 6:   // verser un litre
	{
		UInventoryComponent* Inventory = PlayerState->GetInventory();
		FInventorySlot* HandSlot = Inventory->EditHandSlot();
		const int32 InPitcher = HandSlot ? PitcherLitersOf(HandSlot->ItemId) : -1;
		const FName Held = HandSlot ? LiquidOfPitcher(HandSlot->ItemId) : NAME_None;
		if (InPitcher < 0)
		{
			Why = LOCTEXT("NoPitcherInHand", "Prenez un pichet en main.");
		}
		else if (Op.Op == 5)
		{
			if (InPitcher >= 2 || Container->GetWater() < 1.f - 0.001f || (InPitcher > 0 && Container->GetLiquid() != Held))
			{
				Why = LOCTEXT("CannotDraw", "On ne peut plus puiser.");
			}
			else
			{
				const FName Liquid = Container->GetLiquid();
				Container->SetWater(Container->GetWater() - 1.f);
				HandSlot->ItemId = PitcherFor(Liquid, InPitcher + 1);
				Inventory->CommitChanges();
			}
		}
		else if (InPitcher == 0 || (Container->GetWater() > 0.01f && Container->GetLiquid() != Held) || Container->GetWater() + 1.f > Capacity + 0.001f)
		{
			Why = LOCTEXT("CannotPour", "On ne peut pas le verser là.");
		}
		else
		{
			Container->SetLiquid(Held, Container->GetWater() + 1.f);
			HandSlot->ItemId = PitcherFor(Held, InPitcher - 1);
			Inventory->CommitChanges();
		}
		break;
	}
	case 2:   // une fiole se remplit de breuvage : une potion
	{
		const FBrew* Brew = FindBrew(Container->GetLiquid());
		if (!Brew || Container->GetWater() < VialLiters - 0.001f)
		{
			Why = LOCTEXT("NoMoreBrew", "Plus de breuvage.");
		}
		else if (FInventorySlot* HandVial = PlayerState->GetInventory()->EditHandSlot(); HandVial && HandVial->ItemId == EmptyVial && HandVial->Count > 0)
		{
			// La fiole qu'on tient : elle devient potion (dans les poches, ou à sa place si c'était la dernière).
			if (--HandVial->Count <= 0)
			{
				HandVial->Clear();
				if (!GiveOwned(PlayerState, Brew->Potion))
				{
					HandVial->ItemId = Brew->Potion;
					HandVial->Count = 1;
				}
				PlayerState->GetInventory()->CommitChanges();
			}
			else if (!GiveOwned(PlayerState, Brew->Potion))
			{
				++HandVial->Count;
				Why = LOCTEXT("NoRoom", "Plus de place dans l'inventaire.");
			}
			else
			{
				PlayerState->GetInventory()->CommitChanges();
			}
			if (Why.IsEmpty())
			{
				Container->SetWater(Container->GetWater() - VialLiters);
			}
		}
		else if (!TakeOwned(PlayerState, EmptyVial, 1))
		{
			Why = LOCTEXT("NoVial", "Il vous faut des fioles vides (au General Store).");
		}
		else if (!GiveOwned(PlayerState, Brew->Potion))
		{
			PlayerState->GetInventory()->StoreItem(EmptyVial, 1);
			Why = LOCTEXT("NoRoom", "Plus de place dans l'inventaire.");
		}
		else
		{
			Container->SetWater(Container->GetWater() - VialLiters);
		}
		break;
	}
	default:
		break;
	}
	if (!Why.IsEmpty())
	{
		Stop(Why);
		return;
	}
	if (--Job.Remaining <= 0)
	{
		Stop(FText::GetEmpty());
		return;
	}
	Job.StepEnd = GetGameState<AErewhonGameState>()->GetWorldClock() + Job.StepSeconds;
	Controller->SetCraftJob(Job, Container);
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	GetWorldTimerManager().SetTimer(Controller->CraftTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
	{
		if (WeakController.IsValid())
		{
			StepPour(WeakController.Get());
		}
	}), Job.StepSeconds, false);
}

FText AErewhonGameMode::TryAddIngredient(AErewhonPlayerController* Controller, AStorageActor* Cauldron, FName ItemId, int32 Count)
{
	FText Error = CheckApothecary(Controller);
	if (Error.IsEmpty() && (!AtContainer(Controller, Cauldron) || Cauldron->GetKind() != EStorageKind::Cauldron))
	{
		Error = LOCTEXT("ContainerTooFar", "Vous êtes trop loin.");
	}
	if (!Error.IsEmpty())
	{
		return Error;
	}
	const FItemDef* Def = UItemDatabase::Get(this)->Find(ItemId);
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!Def || !Cauldron->Accepts(*Def))
	{
		return LOCTEXT("NotIngredient", "Cela ne va pas dans le chaudron (pas tant qu'il contient un breuvage).");
	}
	Count = FMath::Min(Count, CountOwned(PlayerState, ItemId));
	if (Count <= 0 || !TakeOwned(PlayerState, ItemId, Count))
	{
		return FText::GetEmpty();
	}
	const int32 Stored = Cauldron->StoreItem(ItemId, Count);
	if (Stored < Count)
	{
		PlayerState->GetInventory()->StoreItem(ItemId, Count - Stored);
		return LOCTEXT("CauldronCrowded", "Le chaudron ne prend pas plus d'ingrédients.");
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryAdjustStand(AErewhonPlayerController* Controller, AStorageActor* Stand, float NewLevel)
{
	const FText Error = CheckApothecary(Controller);
	const APawn* Pawn = Controller->GetPawn();
	const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!Error.IsEmpty())
	{
		return Error;
	}
	if (!Stand || !Pawn || !PlayerState || Stand->GetKind() != EStorageKind::CauldronStand || !Stand->IsOwnedBy(PlayerState->GetPlayerKey())
		|| FVector::Dist(Pawn->GetActorLocation(), Stand->GetActorLocation()) > 500.f)
	{
		return FText::GetEmpty();
	}
	// Le chaudron qui y pend suit la hauteur.
	AStorageActor* Hung = nullptr;
	for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It)
	{
		if (It->GetKind() == EStorageKind::Cauldron && !It->GetCarrier()
			&& FVector::Dist(It->GetActorLocation(), ErewhonApothecary::CauldronSpotOn(Stand).GetLocation()) <= 20.f)
		{
			Hung = *It;
		}
	}
	Stand->SetLevel(NewLevel);
	if (Hung)
	{
		Hung->SetActorLocation(ErewhonApothecary::CauldronSpotOn(Stand).GetLocation());
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryEmptyWater(AErewhonPlayerController* Controller, AStorageActor* Container)
{
	if (!AtContainer(Controller, Container))
	{
		return FText::GetEmpty();
	}
	if (Container->GetKind() == EStorageKind::Cauldron)
	{
		const FText Error = CheckApothecary(Controller);
		if (!Error.IsEmpty())
		{
			return Error;
		}
		for (FInventorySlot& Slot : Container->EditSlots())
		{
			Slot.Clear();
		}
		Container->SetBrewState(Container->GetTemperature(), 0.f);
		Container->Commit();
	}
	Container->SetWater(0.f);
	return FText::GetEmpty();
}
FText AErewhonGameMode::TryBuyStoreroom(AErewhonPlayerController* Controller, const FIntPoint& Sector)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey())
	{
		return LOCTEXT("NotYours", "Ce secteur ne vous appartient pas.");
	}
	if (!(State->GetBuildings(Sector) & ESectorBuilding::Apothecary))
	{
		return LOCTEXT("NeedApothecary", "Il faut d'abord l'apothicairerie.");
	}
	if (State->GetBuildings(Sector) & ESectorBuilding::Storeroom)
	{
		return LOCTEXT("StoreroomBuilt", "La remise est déjà construite.");
	}
	if (const FText NotOne = CheckApothecary(Controller); !NotOne.IsEmpty())
	{
		return NotOne;
	}
	if (!PlayerState->SpendCoins(UErewhonSettings::Get().StoreroomPrice))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	State->AddBuilding(Sector, ESectorBuilding::Storeroom);
	Controller->ClientToast(LOCTEXT("StoreroomDone", "La remise est construite : on y entre par la droite de l'apothicairerie."));
	return FText::GetEmpty();
}

AStorageActor* AErewhonGameMode::SpawnStorage(EStorageKind Kind, const FTransform& At, const FString& OwnerKey, const FString& OwnerName)
{
	AStorageActor* Storage = GetWorld()->SpawnActorDeferred<AStorageActor>(AStorageActor::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Storage->Setup(Kind, OwnerKey, OwnerName);
	Storage->FinishSpawning(At);
	return Storage;
}

void AErewhonGameMode::SpawnFurnishing(const FIntPoint& Sector, const FString& OwnerKey, const FString& OwnerName, const TArray<FInventorySlot>& Shelves)
{
	const FTransform House = ErewhonApothecary::HouseTransform(Sector, GetGameState<AErewhonGameState>()->GetWorldSeed());
	const auto At = [&House](const FVector& Local, float Yaw) { return FTransform(FRotator(0.f, Yaw, 0.f), Local) * House; };
	const float Floor = ErewhonSpawn::CabinFloorTop;
	// La table au milieu, dans la largeur ; le mortier dessus.
	AStorageActor* Table = SpawnStorage(EStorageKind::Table, At(FVector(40.f, 0.f, Floor), 0.f), OwnerKey, OwnerName);
	SpawnStorage(EStorageKind::Mortar, At(FVector(40.f, -45.f, Floor + 82.f), 0.f), OwnerKey, OwnerName)->StackOn(Table);
	// Le support dans la cheminée, le chaudron dessus.
	if (const TOptional<FTransform> Spot = ErewhonApothecary::StandSpot(this, Sector))
	{
		AStorageActor* Stand = SpawnStorage(EStorageKind::CauldronStand, *Spot, OwnerKey, OwnerName);
		SpawnStorage(EStorageKind::Cauldron, ErewhonApothecary::CauldronSpotOn(Stand), OwnerKey, OwnerName);
	}
	// Trois étagères contre le mur de droite, de part et d'autre de la fenêtre, façade vers la pièce.
	const float X[] = { -320.f, -180.f, 230.f };
	for (int32 i = 0; i < 3; ++i)
	{
		AStorageActor* Shelf = SpawnStorage(EStorageKind::Shelf,
			At(FVector(X[i], ErewhonSpawn::CabinHalfWidth - ErewhonSpawn::CabinWallThickness - 20.f, Floor), 90.f), OwnerKey, OwnerName);
		for (int32 Row = 0; Row < ErewhonApothecary::ShelfRows; ++Row)
		{
			const int32 Old = i * ErewhonApothecary::ShelfRows + Row;
			if (Shelves.IsValidIndex(Old) && !Shelves[Old].IsEmpty())
			{
				Shelf->EditSlots()[Row * ErewhonApothecary::ShelfSlotsPerRow] = Shelves[Old];
			}
		}
		Shelf->Commit();
	}
}

FText AErewhonGameMode::TrySellSector(AErewhonPlayerController* Controller, const FIntPoint& Sector)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
	}
	// À la mairie de son village, et sans couper du village les secteurs qu'il y relie à la place.
	const FIntPoint Village = ErewhonVillages::NearestVillage(ErewhonSector::FromWorld(Controller->GetPawn()->GetActorLocation()));
	const FText Why = State->WhyNotSellable(Sector, Village, PlayerState);
	if (!Why.IsEmpty())
	{
		return Why;
	}
	State->ClearFieldSector(Sector);
	State->ClearSectorOwner(Sector);
	PlayerState->AddCoins(UErewhonSettings::Get().SectorSellPrice);
	// Ce qu'on y avait construit part avec le secteur.
	if (TObjectPtr<AApothecary> House; Apothecaries.RemoveAndCopyValue(Sector, House) && House)
	{
		House->Destroy();
	}
	return FText::GetEmpty();
}

TOptional<FVector> AErewhonGameMode::FindWorkshopSpot(const FIntPoint& Village) const
{
	const FTransform Cabin = ErewhonSpawn::CabinTransform(Village, EShopKind::Workshop);
	const FVector2D HalfSize = ErewhonSpawn::CabinHalfSize(EShopKind::Workshop);
	// Devant la porte, sur deux rangs, en s'écartant de part et d'autre : la première place libre.
	for (const float Out : { 480.f, 820.f })
	{
		for (const float Side : { 0.f, 320.f, -320.f, 640.f, -640.f })
		{
			const FVector Candidate = Cabin.TransformPosition(FVector(-HalfSize.X - Out, Side, 0.f));
			bool bFree = true;
			for (TActorIterator<AActor> It(GetWorld()); It && bFree; ++It)
			{
				if ((It->IsA<ACart>() || It->IsA<APawn>()) && FVector::Dist2D(It->GetActorLocation(), Candidate) < 280.f)
				{
					bFree = false;
				}
			}
			if (bFree)
			{
				return Candidate;
			}
		}
	}
	return {};
}

void AErewhonGameMode::DeliverCart(AErewhonPlayerState* PlayerState, FName ItemId, const FVector& Spot)
{
	const AErewhonGameState* State = GetGameState<AErewhonGameState>();
	// En travers, le long de la façade de l'atelier du village où on la livre.
	const float Yaw = ErewhonSpawn::CabinTransform(ErewhonSpawn::VillageAt(Spot), EShopKind::Workshop).GetRotation().Rotator().Yaw + 90.f;
	const FVector2D Foot(Spot.X, Spot.Y);
	const FTransform Where(FRotator(0.f, Yaw, 0.f), FVector(Foot, ErewhonTerrain::Height(Foot, State ? State->GetWorldSeed() : 0)));
	ACart* Cart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), Where);
	Cart->Setup(PlayerState->GetPlayerKey(), PlayerState->GetPlayerName(), ErewhonPlaceables::CartKindForItem(ItemId).Get(ECartKind::Cart));
	Cart->FinishSpawning(Where);
}

FText AErewhonGameMode::TryBuyItem(AErewhonPlayerController* Controller, FName ItemId)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const FItemDef* Def = UItemDatabase::Get(this)->Find(ItemId);
	// Les commerces du village où se tient le joueur.
	const FIntPoint Village = PlayerVillage(Controller);
	if (!PlayerState || !PlayerState->IsRegistered() || !Def || !IsNearMerchant(Controller, GetGameState<AErewhonGameState>()->ShopFor(Village, *Def)))
	{
		return LOCTEXT("TooFar", "Le marchand est trop loin.");
	}
	// La même règle que l'étal (voir AErewhonGameState::WhyNotOffered) : commerce, classe, objet déjà à soi.
	const FText NotOffered = GetGameState<AErewhonGameState>()->WhyNotOffered(Village, *Def, GetGameState<AErewhonGameState>()->ShopFor(Village, *Def), PlayerState);
	if (!NotOffered.IsEmpty())
	{
		return NotOffered;
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	if (Def->IsBackpack())
	{
		// Le sac se porte aussitôt sur le dos : il n'entre pas dans l'inventaire.
		UBackpackComponent* Bag = PlayerState->GetBackpack();
		if (!PlayerState->SpendCoins(Def->BuyPrice))
		{
			return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
		}
		Bag->Grant();
		Controller->ClientToast(LOCTEXT("BagBought", "Le sac est sur votre dos. [F] pour le poser au sol, puis [E] pour l'ouvrir."));
		return FText::GetEmpty();
	}
	// Charrettes et outils attelés : trop lourds pour les poches, l'atelier les livre devant sa porte.
	if (ErewhonPlaceables::IsCart(ItemId))
	{
		const TOptional<FVector> Spot = FindWorkshopSpot(Village);
		if (!Spot.IsSet())
		{
			return LOCTEXT("YardFull", "Il n'y a plus de place devant l'atelier : emmenez d'abord ce qui y attend.");
		}
		if (!PlayerState->SpendCoins(Def->BuyPrice))
		{
			return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
		}
		DeliverCart(PlayerState, ItemId, *Spot);
		Controller->ClientToast(FText::Format(LOCTEXT("Delivered", "{0} vous attend devant l'atelier."), Def->Name));
		return FText::GetEmpty();
	}
	if (!Inventory->CanStore(ItemId, 1))
	{
		return LOCTEXT("InventoryFull", "Inventaire plein.");
	}
	// Rien qu'on ne puisse plus porter : on resterait planté là, chez le marchand.
	if (const AErewhonCharacter* Buyer = Controller->GetPawn<AErewhonCharacter>();
		Buyer && Buyer->GetCarriedWeight() + Def->GetWeight() > Buyer->GetCarryMax())
	{
		return FText::Format(LOCTEXT("TooHeavyToBuy", "Trop lourd à porter ({0} kg) : faites de la place, ou venez à dos de bhurak."),
			FText::AsNumber(FMath::RoundToInt32(Def->GetWeight())));
	}
	if (!PlayerState->SpendCoins(Def->BuyPrice))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	Inventory->StoreItem(ItemId, 1);
	return FText::GetEmpty();
}

FText AErewhonGameMode::Unstuck(AErewhonPlayerController* Controller)
{
	AErewhonCharacter* Character = Controller->GetPawn<AErewhonCharacter>();
	if (!Character)
	{
		return FText::GetEmpty();
	}
	if (Character->GetMount())
	{
		TryRideBhurak(Controller, nullptr);   // pied à terre
	}
	if (ACart* Driven = Character->GetDrivenCart())
	{
		Driven->StopDriving();
		Character->SetDrivenCart(nullptr);
	}
	// Une charrette ou un outil attelé en poche (achetés avant la livraison) : livrés devant l'atelier,
	// et l'on se retrouve devant la mairie, du village où l'on est.
	const FIntPoint Village = PlayerVillage(Controller);
	if (AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>())
	{
		UInventoryComponent* Inventory = PlayerState->GetInventory();
		bool bChanged = false;
		for (FInventorySlot& Slot : Inventory->EditSlots())
		{
			while (!Slot.IsEmpty() && ErewhonPlaceables::IsCart(Slot.ItemId) && Slot.Count > 0)
			{
				const TOptional<FVector> Spot = FindWorkshopSpot(Village);
				if (!Spot.IsSet())
				{
					break;
				}
				DeliverCart(PlayerState, Slot.ItemId, *Spot);
				Controller->ClientToast(LOCTEXT("DeliveredBack", "Ce que vous portiez d'attelable vous attend devant l'atelier."));
				if (--Slot.Count <= 0)
				{
					Slot.Clear();
				}
				bChanged = true;
			}
		}
		if (bChanged)
		{
			Inventory->CommitChanges();
		}
	}
	// Devant la mairie : un peu vers le centre de la place, face au comptoir.
	const FVector Hall = ErewhonSpawn::MerchantLocation(Village, EShopKind::SectorManager);
	const FVector2D Toward = (FVector2D(ErewhonSector::Center(ErewhonVillages::CenterOf(Village))) - FVector2D(Hall)).GetSafeNormal();
	const FVector2D Foot = FVector2D(Hall) + Toward * 350.f;
	const AErewhonGameState* State = GetGameState<AErewhonGameState>();
	const float Ground = ErewhonTerrain::Height(Foot, State ? State->GetWorldSeed() : 0);
	const FRotator Facing(0.f, FMath::RadiansToDegrees(FMath::Atan2(-Toward.Y, -Toward.X)), 0.f);
	Character->TeleportTo(FVector(Foot, Ground + Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.f), Facing, false, true);
	Controller->ClientSetRotation(Facing);
	// Trop chargé, on ne bougerait pas plus ici : le dire.
	if (Character->GetCarriedWeight() > Character->GetCarryMax())
	{
		return FText::Format(LOCTEXT("UnstuckHeavy", "Vous êtes trop chargé pour avancer ({0} / {1} kg) : jetez ou vendez quelque chose (inventaire)."),
			FText::AsNumber(FMath::RoundToInt32(Character->GetCarriedWeight())), FText::AsNumber(FMath::RoundToInt32(Character->GetCarryMax())));
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TrySellItem(AErewhonPlayerController* Controller, FName ItemId, int32 Count)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const FItemDef* Def = UItemDatabase::Get(this)->Find(ItemId);
	const FIntPoint Village = PlayerVillage(Controller);
	if (!PlayerState || !PlayerState->IsRegistered() || !Def || !IsNearMerchant(Controller, GetGameState<AErewhonGameState>()->ShopFor(Village, *Def)))
	{
		return LOCTEXT("TooFar", "Le marchand est trop loin.");
	}
	const int32 Price = Def->ResalePrice(UErewhonSettings::Get().ResaleRatio);
	if (!GetGameState<AErewhonGameState>()->BuysBack(Village, *Def, GetGameState<AErewhonGameState>()->ShopFor(Village, *Def)))
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

FText AErewhonGameMode::TryCheatSetWeather(AErewhonPlayerController* Controller, uint8 Weather)
{
	if (!CanCheat(Controller))
	{
		return LOCTEXT("NoCheat", "La triche est réservée à l'hôte de la partie.");
	}
	TOptional<ErewhonWeather::EKind> Kind;
	if (Weather != 0)
	{
		Kind = static_cast<ErewhonWeather::EKind>(FMath::Min<uint8>(Weather - 1, static_cast<uint8>(ErewhonWeather::EKind::Storm)));
	}
	GetGameState<AErewhonGameState>()->SetForcedWeather(Kind);
	return FText::GetEmpty();
}

// ── Village ──────────────────────────────────────────────────────────────────

FText AErewhonGameMode::TryBuyVillageUpgrade(AErewhonPlayerController* Controller, uint8 UpgradeIndex)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
	}
	if (UpgradeIndex >= static_cast<uint8>(EVillageUpgrade::Count))
	{
		return FText::GetEmpty();
	}
	// L'amélioration du village de cette mairie.
	const FIntPoint Village = PlayerVillage(Controller);
	const EVillageUpgrade Upgrade = static_cast<EVillageUpgrade>(UpgradeIndex);
	const ErewhonVillage::FUpgradeInfo& Info = ErewhonVillage::Info(Upgrade);
	if (State->HasVillageUpgrade(Village, Upgrade))
	{
		return LOCTEXT("AlreadyBuilt", "C'est déjà fait !");
	}
	if (Info.Requires != EVillageUpgrade::Count && !State->HasVillageUpgrade(Village, Info.Requires))
	{
		return FText::Format(LOCTEXT("RequiresFirst", "Il faut d'abord : {0}."), ErewhonVillage::Info(Info.Requires).Name);
	}
	if (!PlayerState->SpendCoins(Info.Price))
	{
		return LOCTEXT("NotEnoughCoins", "Pas assez de pièces.");
	}
	State->AddVillageUpgrade(Village, Upgrade);
	SpawnDoors(Village);   // une boutique de plus : sa porte
	SaveWorld();
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryBuyPavedLink(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 Side)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!PlayerState || !PlayerState->IsRegistered() || !IsNearMerchant(Controller, EShopKind::SectorManager))
	{
		return LOCTEXT("ManagerTooFar", "La mairie est trop loin.");
	}
	const FString* OwnerKey = State->FindSectorOwnerKey(Sector);
	if (!OwnerKey || *OwnerKey != PlayerState->GetPlayerKey() || Side < 0 || Side >= 4)
	{
		return LOCTEXT("NotYours", "Ce secteur ne vous appartient pas.");
	}
	if (!State->IsVillagePaved(ErewhonVillages::NearestVillage(Sector)))
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
		DropOnGround(Pawn, Slot.ItemId, Slot.Count);
		Slot.Clear();
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
	// Toute la pile, posée à terre devant soi.
	DropOnGround(Controller->GetPawn(), Slot.ItemId, Slot.Count);
	Slot.Clear();
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

void AErewhonGameMode::DropOnGround(const APawn* Pawn, FName ItemId, int32 Count)
{
	AErewhonGameState* State = GetGameState<AErewhonGameState>();
	if (!Pawn || ItemId.IsNone() || Count <= 0 || !State)
	{
		return;
	}
	// Devant soi, au sol ; sur le terrain de quelqu'un, il est à lui ; ailleurs, à tous.
	FVector Spot = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 90.f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonDrop), false, Pawn);
	// Le sol (un plancher, une dalle) sous ses pieds ; à défaut, le relief du terrain (le secteur peut être en train de se reconstruire).
	if (GetWorld()->LineTraceSingleByChannel(Hit, Spot + FVector(0, 0, 50.f), Spot - FVector(0, 0, 400.f), ECC_WorldStatic, Params))
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
	const double ExpiresAt = State->GetWorldClock() + ADroppedItem::LifeSeconds;
	// Le même objet au même endroit : il rejoint le tas.
	for (TActorIterator<ADroppedItem> It(GetWorld()); It; ++It)
	{
		if (It->GetItemId() == ItemId && It->CanPickUp(OwnerKey) && FVector::Dist2D(It->GetActorLocation(), Spot) < 120.f)
		{
			It->AddCount(Count, ExpiresAt);
			return;
		}
	}
	const FTransform At(FRotator::ZeroRotator, Spot + FVector(0.f, 0.f, 16.f));
	ADroppedItem* Dropped = GetWorld()->SpawnActorDeferred<ADroppedItem>(ADroppedItem::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Dropped->Setup(ItemId, Count, OwnerKey, LandOwner ? State->GetSectorOwnerName(Sector) : FString(), ExpiresAt);
	Dropped->FinishSpawning(At);
}

FText AErewhonGameMode::TryPickUpDropped(AErewhonPlayerController* Controller, ADroppedItem* Dropped)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	if (!PlayerState || !Pawn || !Dropped || FVector::Dist(Pawn->GetActorLocation(), Dropped->GetActorLocation()) > 500.f)
	{
		return FText::GetEmpty();
	}
	if (!Dropped->CanPickUp(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("DroppedNotYours", "C'est sur le terrain de quelqu'un d'autre : seul son propriétaire peut le ramasser.");
	}
	// Les bûches : deux dans les bras, pas plus.
	const bool bLogs = Dropped->GetItemId() == ErewhonForestry::Log;
	const int32 Wanted = bLogs ? FMath::Min(Dropped->GetCount(), ErewhonForestry::RoomForLogs(PlayerState->GetInventory())) : Dropped->GetCount();
	if (bLogs && Wanted <= 0)
	{
		return LOCTEXT("ArmsFullDrop", "Vos bras sont pleins : on ne porte que deux bûches.");
	}
	const int32 Stored = PlayerState->GetInventory()->StoreItem(Dropped->GetItemId(), Wanted);
	if (Stored <= 0)
	{
		return LOCTEXT("InventoryFull", "Inventaire plein.");
	}
	Dropped->Take(Stored);
	return FText::GetEmpty();
}

// ── Charrette et conteneurs ──────────────────────────────────────────────────

namespace
{
	bool CanReachStorage(const AErewhonPlayerState* Owner, const APawn* Pawn, const AStorageActor* Storage)
	{
		return Owner && Pawn && Storage && (Storage->IsOwnedBy(Owner->GetPlayerKey()) || Storage->GetKind() == EStorageKind::FirewoodRack)
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
	if (Storage->GetKind() == EStorageKind::Saddlebag)
	{
		return FText::GetEmpty();   // elle reste sur le harnais
	}
	if (Character->GetCarried() || Character->GetDrivenCart() || Character->GetMount())
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
	{
		const FName Held = PlayerState->GetInventory()->GetHandItem();
		const AStorageActor* Carried = Character->GetCarried();
		const bool bCartItem = !Carried && ErewhonPlaceables::IsCart(Held);
		const TOptional<EStorageKind> HeldKind = Carried ? TOptional<EStorageKind>(Carried->GetKind()) : ErewhonPlaceables::StorageForItem(Held);
		if (!bCartItem && HeldKind.IsSet() && !ErewhonPlaceables::CanPlaceAt(this, PlayerState->GetPlayerKey(), false, *HeldKind, Type, Where))
		{
			return LOCTEXT("NotYourSector", "Seulement sur vos secteurs (ou, pour une cagette, dans une charrette).");
		}
	}
	if (FVector::Dist(Character->GetActorLocation(), Where) > 700.f)
	{
		return LOCTEXT("TooFarToPlace", "C'est trop loin.");
	}

	// Ce qu'on pose : le conteneur qu'on porte, ou l'objet qu'on tient en main.
	AStorageActor* Storage = Character->GetCarried();
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	const FName HandItem = Inventory->GetHandItem();
	const bool bFromHand = !Storage;
	if (bFromHand && ErewhonPlaceables::IsBhurak(HandItem))
	{
		if (Type != 0)
		{
			return FText::GetEmpty();
		}
		SpawnBhurak(FTransform(FRotator(0.f, Yaw, 0.f), Location), PlayerState->GetPlayerKey(), PlayerState->GetPlayerName(),
			static_cast<uint8>(FMath::Max(0, ABhurak::CoatOfItem(HandItem))));
	}
	else if (bFromHand && ErewhonPlaceables::IsCart(HandItem))
	{
		if (Type != 0)
		{
			return FText::GetEmpty();
		}
		ACart* Cart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), Location));
		Cart->Setup(PlayerState->GetPlayerKey(), PlayerState->GetPlayerName(), ErewhonPlaceables::CartKindForItem(HandItem).Get(ECartKind::Cart));
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
		// Des bûches en main : un tas neuf au sol, ou sur un tas à soi.
		if (bFromHand && Kind == EStorageKind::LogPile)
		{
			FInventorySlot* LogSlot = Inventory->EditHandSlot();
			const int32 Held = LogSlot && LogSlot->ItemId == ErewhonForestry::Log ? LogSlot->Count : 0;
			AStorageActor* Pile = Type == 1 ? Cast<AStorageActor>(Target) : nullptr;
			if (Held <= 0 || Type == 2)
			{
				return Type == 2 ? LOCTEXT("LogsByTheSide", "Les bûches se chargent par le côté de la charrette : [F] pour l'ouvrir.") : FText::GetEmpty();
			}
			if (Type == 1 && (!Pile || Pile->GetKind() != EStorageKind::LogPile || !Pile->IsOwnedBy(PlayerState->GetPlayerKey())))
			{
				return LOCTEXT("NotAPile", "On ne les pose que sur un de ses tas de bûches.");
			}
			if (Pile && Pile->CountStored(ErewhonForestry::Log) >= ErewhonForestry::PileCapacity)
			{
				return LOCTEXT("PileFull", "Ce tas est complet : vingt bûches.");
			}
			if (!Pile)
			{
				const FTransform Spawn(FRotator(0.f, Yaw, 0.f), Location);
				Pile = GetWorld()->SpawnActorDeferred<AStorageActor>(AStorageActor::StaticClass(), Spawn);
				Pile->Setup(EStorageKind::LogPile, PlayerState->GetPlayerKey(), PlayerState->GetPlayerName());
				Pile->FinishSpawning(Spawn);
				Pile->PlaceOnGround(Location, Yaw);
			}
			const int32 Put = Pile->StoreItem(ErewhonForestry::Log, Held);
			LogSlot->Count -= Put;
			if (LogSlot->Count <= 0)
			{
				LogSlot->Clear();
			}
			Inventory->CommitChanges();
			Pile->Commit();
			return FText::GetEmpty();
		}
		AStorageActor* Base = Type == 1 ? Cast<AStorageActor>(Target) : nullptr;
		ACart* Cart = Type == 2 ? Cast<ACart>(Target) : nullptr;
		const bool bMortarOnTable = Type == 1 && Base && Kind == EStorageKind::Mortar && Base->GetKind() == EStorageKind::Table && !Base->GetAbove();
		if (Type == 1 && !bMortarOnTable && (!Base || Kind != EStorageKind::Crate || Base->GetKind() != EStorageKind::Crate || !Base->IsOwnedBy(PlayerState->GetPlayerKey())
			|| Base->TopOfStack()->StackSize() >= ErewhonPlaceables::MaxStack || Base->TopOfStack()->GetCarrier()))
		{
			return LOCTEXT("CannotStack", "On ne peut pas l'empiler là.");
		}
		if (Type == 1 && Base && !Base->IsOwnedBy(PlayerState->GetPlayerKey()))
		{
			return LOCTEXT("CannotStack", "On ne peut pas l'empiler là.");
		}
		if (Type == 2 && ErewhonPlaceables::IsFurniture(Kind))
		{
			return LOCTEXT("NoFurnitureInCart", "Le mobilier ne va pas dans la charrette.");
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
			if (Kind == EStorageKind::Potpourri)
			{
				Storage->SetExpiresAt(GetGameState<AErewhonGameState>()->GetWorldClock() + ErewhonPlaceables::PotpourriSeconds);
			}
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
		else if (const TOptional<FTransform> Hook = ErewhonPlaceables::HookFor(this, Kind, Location))
		{
			Storage->PlaceOnGround(Hook->GetLocation(), Hook->Rotator().Yaw);   // le chaudron pend à la crémaillère
		}
		else if (const TOptional<FVector> Under = ErewhonPlaceables::FountainSnap(this, Kind, Location))
		{
			Storage->PlaceOnGround(*Under, Yaw);   // sous le bec de la fontaine
		}
		else
		{
			Storage->PlaceOnGround(Location, Yaw);
		}
	}

	if (bFromHand)
	{
		// L'objet quitte la main.
		if (FInventorySlot* HandSlot = Inventory->EditHandSlot(); HandSlot && --HandSlot->Count <= 0)
		{
			HandSlot->Clear();
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
	if (Cart && Cast<ABhurak>(Cart->GetDriver()))
	{
		return LOCTEXT("CartTowedDrive", "Elle est attelée : [G] en visant le bhurak pour la dételer.");
	}
	if (!Cart || Cart->GetDriver())
	{
		return FText::GetEmpty();
	}
	if (!Cart->IsOwnedBy(PlayerState->GetPlayerKey()))
	{
		return LOCTEXT("NotYourCart", "Cette charrette n'est pas à vous.");
	}
	if (Character->GetCarried() || Character->GetMount())
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
	if (Storage && Storage->GetKind() == EStorageKind::DryingRack)
	{
		if (const FText NotOne = CheckApothecary(Controller); !NotOne.IsEmpty())
		{
			return NotOne;
		}
	}
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
		return Storage->GetKind() == EStorageKind::Shelf ? LOCTEXT("ShelfOnly", "L'étagère est pour les potions et les écrasés.")
			: LOCTEXT("PlantsOnly", "On n'y range que des plantes.");
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
			const int32 Moved = FMath::Min(A.Count, (bToStorage ? Storage->SlotCap(*Def) : Def->MaxStack) - (B.IsEmpty() ? 0 : B.Count));
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
			if (!Other || (bFromStorage && (!Storage->Accepts(*Other) || B.Count > Storage->SlotCap(*Other)))
				|| (bToStorage && A.Count > Storage->SlotCap(*Def)))
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

FText AErewhonGameMode::TryMoveCartItem(AErewhonPlayerController* Controller, ACart* Cart, bool bFromCart, int32 From, bool bToCart, int32 To)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!PlayerState || !Pawn || !Cart || !Items || !Cart->IsOwnedBy(PlayerState->GetPlayerKey())
		|| FVector::Dist(Pawn->GetActorLocation(), Cart->GetActorLocation()) > 500.f)
	{
		return LOCTEXT("StorageTooFar", "C'est trop loin.");
	}
	if (bToCart && Cart->HasLoads())
	{
		return LOCTEXT("CartOccupied", "La charrette porte déjà des conteneurs.");
	}
	if (bToCart && Cart->GetCartKind() == ECartKind::Cultivator)
	{
		return LOCTEXT("CultivatorNoLoad", "Le cultivateur ne porte rien.");
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	TArray<FInventorySlot>& Source = bFromCart ? Cart->EditWood() : Inventory->EditSlots();
	TArray<FInventorySlot>& Target = bToCart ? Cart->EditWood() : Inventory->EditSlots();
	if (!Source.IsValidIndex(From) || Source[From].IsEmpty() || (!bFromCart && Source[From].Kind != ESlotKind::Inventory))
	{
		return FText::GetEmpty();
	}
	const FName ItemId = Source[From].ItemId;
	if (bToCart && !Cart->AcceptsBulk(ItemId))
	{
		return Cart->GetCartKind() == ECartKind::Seeder ? LOCTEXT("SeedsOnly", "La trémie ne prend que des sacs de graines.")
			: Cart->GetCartKind() == ECartKind::Harvester ? LOCTEXT("HopperOnly", "Le coffre se remplit tout seul, en moissonnant.")
			: LOCTEXT("WoodOnly", "En vrac, la charrette ne porte que du petit bois ou des bûches.");
	}
	const FItemDef* Def = Items->Find(ItemId);
	const int32 Cap = Def ? FMath::Min(Def->MaxStack, 10) : 10;
	int32 Moved = 0;
	if (To == INDEX_NONE)
	{
		if (bToCart)
		{
			// Sur les piles de petit bois, puis sur les places vides.
			for (const bool bEmptyPass : { false, true })
			{
				for (FInventorySlot& Slot : Target)
				{
					if (Moved >= Source[From].Count || Slot.IsEmpty() != bEmptyPass || (!bEmptyPass && Slot.ItemId != ItemId))
					{
						continue;
					}
					const int32 Put = FMath::Min(Cap - (bEmptyPass ? 0 : Slot.Count), Source[From].Count - Moved);
					Slot.Count = (bEmptyPass ? 0 : Slot.Count) + Put;
					Slot.ItemId = ItemId;
					Moved += Put;
				}
			}
		}
		else
		{
			// Les bûches : deux dans les bras, pas plus.
			Moved = Inventory->StoreItem(ItemId, ItemId == ErewhonForestry::Log ? FMath::Min(Source[From].Count, ErewhonForestry::RoomForLogs(Inventory)) : Source[From].Count);
		}
	}
	else if (Target.IsValidIndex(To) && (Target[To].IsEmpty() || Target[To].ItemId == ItemId) && (bToCart || Target[To].Kind == ESlotKind::Inventory))
	{
		Moved = FMath::Min(Source[From].Count, (bToCart ? Cap : Def->MaxStack) - (Target[To].IsEmpty() ? 0 : Target[To].Count));
		if (!bToCart && ItemId == ErewhonForestry::Log)
		{
			Moved = FMath::Min(Moved, ErewhonForestry::RoomForLogs(Inventory));
		}
		if (Moved > 0)
		{
			Target[To].Count = (Target[To].IsEmpty() ? 0 : Target[To].Count) + Moved;
			Target[To].ItemId = ItemId;
		}
	}
	if (Moved <= 0)
	{
		return bToCart ? LOCTEXT("CartFull", "La charrette est pleine.") : LOCTEXT("InventoryFull", "Inventaire plein.");
	}
	FInventorySlot& Origin = (bFromCart ? Cart->EditWood() : Inventory->EditSlots())[From];
	Origin.Count -= Moved;
	if (Origin.Count <= 0)
	{
		Origin.Clear();
	}
	Inventory->CommitChanges();
	Cart->CommitWood();
	return FText::GetEmpty();
}

FText AErewhonGameMode::TryStashInCart(AErewhonPlayerController* Controller, ACart* Cart)
{
	// Tout le petit bois des poches.
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	if (!PlayerState)
	{
		return FText::GetEmpty();
	}
	const TArray<FInventorySlot>& Slots = PlayerState->GetInventory()->GetSlots();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].Kind == ESlotKind::Inventory && Cart && Cart->AcceptsBulk(Slots[i].ItemId) && !Slots[i].IsEmpty())
		{
			const FText Error = TryMoveCartItem(Controller, Cart, false, i, true, INDEX_NONE);
			if (!Error.IsEmpty())
			{
				return Error;
			}
		}
	}
	return FText::GetEmpty();
}

FText AErewhonGameMode::TrySplitMove(AErewhonPlayerController* Controller, bool bFromOther, int32 From, bool bToOther, int32 To, int32 Count,
	AStorageActor* Storage, ACart* Cart)
{
	AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const APawn* Pawn = Controller->GetPawn();
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!PlayerState || !Pawn || !Items || Count <= 0)
	{
		return FText::GetEmpty();
	}
	// L'autre côté : un conteneur, la charrette, ou le sac (porté, ou posé tout près).
	UBackpackComponent* Bag = PlayerState->GetBackpack();
	TArray<FInventorySlot>* Other = nullptr;
	if (Storage)
	{
		if (!CanReachStorage(PlayerState, Pawn, Storage))
		{
			return LOCTEXT("StorageTooFar", "C'est trop loin.");
		}
		Other = &Storage->EditSlots();
	}
	else if (Cart)
	{
		if (!Cart->IsOwnedBy(PlayerState->GetPlayerKey()) || FVector::Dist(Pawn->GetActorLocation(), Cart->GetActorLocation()) > 500.f)
		{
			return LOCTEXT("StorageTooFar", "C'est trop loin.");
		}
		Other = &Cart->EditWood();
	}
	else if (Bag->IsOwned() && Bag->IsAccessibleFrom(Pawn))
	{
		Other = &Bag->EditSlots();
	}
	if ((bFromOther || bToOther) && !Other)
	{
		return FText::GetEmpty();
	}
	UInventoryComponent* Inventory = PlayerState->GetInventory();
	TArray<FInventorySlot>& Source = bFromOther ? *Other : Inventory->EditSlots();
	TArray<FInventorySlot>& Target = bToOther ? *Other : Inventory->EditSlots();
	if (!Source.IsValidIndex(From) || !Target.IsValidIndex(To) || Source[From].IsEmpty() || (&Source == &Target && From == To))
	{
		return FText::GetEmpty();
	}
	FInventorySlot& Src = Source[From];
	FInventorySlot& Dst = Target[To];
	const FItemDef* Def = Items->Find(Src.ItemId);
	if (!Def || (!Dst.IsEmpty() && Dst.ItemId != Src.ItemId) || Src.Kind == ESlotKind::Clothing || Dst.Kind == ESlotKind::Clothing)
	{
		return LOCTEXT("SlotTaken", "Cette case est prise.");
	}
	// Ce que la case d'arrivée accepte, et combien.
	int32 Cap = Def->MaxStack;
	if (!bToOther)
	{
		if (Dst.Kind == ESlotKind::Equipment && !Def->IsEquipable())
		{
			return LOCTEXT("NotEquipable", "Seuls les outils et ce qui se tient en main vont dans la barre.");
		}
	}
	else if (Storage)
	{
		if (!Storage->Accepts(*Def))
		{
			return LOCTEXT("NotAccepted", "Cela ne va pas là-dedans.");
		}
		Cap = Storage->SlotCap(*Def);
	}
	else if (Cart)
	{
		if (!Cart->AcceptsBulk(Src.ItemId) || Cart->HasLoads())
		{
			return LOCTEXT("NotAccepted", "Cela ne va pas là-dedans.");
		}
		Cap = FMath::Min(Def->MaxStack, 10);
	}
	else if (Dst.Kind == ESlotKind::Equipment && Def->Category != EItemCategory::Tool)
	{
		return LOCTEXT("ToolsOnly", "Seuls les outils s'accrochent sur le côté du sac.");
	}
	const int32 Moved = FMath::Min(FMath::Min(Count, Src.Count), Cap - (Dst.IsEmpty() ? 0 : Dst.Count));
	if (Moved <= 0)
	{
		return LOCTEXT("SlotFull", "Plus de place dans cette case.");
	}
	Dst.ItemId = Src.ItemId;
	Dst.Count = (Dst.IsEmpty() ? 0 : Dst.Count) + Moved;
	Src.Count -= Moved;
	if (Src.Count <= 0)
	{
		Src.Clear();
	}
	Inventory->CommitChanges();
	if (Storage)
	{
		Storage->Commit();
	}
	else if (Cart)
	{
		Cart->CommitWood();
	}
	else if (Other)
	{
		Bag->Commit();
	}
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
	if (ABhurak* Mount = Character->GetMount())
	{
		Mount->SetRider(nullptr);
		Character->SetMount(nullptr);
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
		WorldSave->Carts.Add({ It->GetOwnerKey(), It->GetOwnerName(), FTransform(FRotator(0.f, It->GetActorRotation().Yaw, 0.f), It->GetActorLocation()), It->GetWood(),
			static_cast<uint8>(It->GetCartKind()), It->IsEngaged() });
	}
	// Les troncs couchés.
	WorldSave->FelledTrees.Reset();
	for (TActorIterator<AFelledTree> It(GetWorld()); It; ++It)
	{
		FSavedFelledTree& Saved = WorldSave->FelledTrees.AddDefaulted_GetRef();
		Saved.Location = It->GetActorLocation();
		Saved.Model = It->GetModel();
		Saved.Height = It->GetTreeHeight();
		Saved.Girth = It->GetGirth();
		Saved.TreeYaw = It->GetTreeYaw();
		Saved.FallYaw = It->GetFallYaw();
		Saved.LogsLeft = It->GetLogsLeft();
		Saved.OwnerKey = It->GetOwnerKey();
		Saved.OwnerName = It->GetOwnerName();
	}
	// Du bas des piles vers le haut : une cagette est toujours enregistrée après celle qui la porte.
	TArray<AStorageActor*> Ordered;
	WorldSave->Bhuraks.Reset();
	for (TActorIterator<ABhurak> It(GetWorld()); It; ++It)
	{
		FSavedBhurak& Saved = WorldSave->Bhuraks.AddDefaulted_GetRef();
		Saved.OwnerKey = It->GetOwnerKey();
		Saved.OwnerName = It->GetOwnerName();
		Saved.Transform = FTransform(FRotator(0.f, It->GetActorRotation().Yaw, 0.f), It->GetActorLocation());
		Saved.bHarness = It->HasHarness();
		Saved.Coat = It->GetCoat();
		for (int32 Slot = 0; Slot < ABhurak::GearSlots; ++Slot)
		{
			Saved.Gear.Add(It->GetGear(Slot));
			const AStorageActor* Bag = It->GetBag(Slot);
			TArray<FInventorySlot> Contents = Bag ? Bag->GetSlots() : TArray<FInventorySlot>();
			Contents.SetNum(ErewhonPlaceables::Capacity(EStorageKind::Saddlebag));
			Saved.BagSlots.Append(Contents);
		}
	}
	for (TActorIterator<AStorageActor> It(GetWorld()); It; ++It)
	{
		if (It->GetKind() == EStorageKind::Saddlebag)
		{
			continue;
		}
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
		Saved.SlotTimes = Storage->GetSlotTimes();
		Saved.ExpiresAt = Storage->GetExpiresAt();
		Saved.Water = Storage->GetWater();
		Saved.Liquid = Storage->GetLiquid();
		Saved.Temperature = Storage->GetTemperature();
		Saved.Level = Storage->GetLevel();
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
	// Les troncs couchés : déjà tombés depuis longtemps.
	for (const FSavedFelledTree& Saved : WorldSave->FelledTrees)
	{
		if (Saved.LogsLeft <= 0)
		{
			continue;
		}
		FTreeInstance Tree;
		Tree.Model = Saved.Model;
		Tree.Height = Saved.Height;
		Tree.Girth = Saved.Girth;
		Tree.Yaw = Saved.TreeYaw;
		const FTransform At(FRotator::ZeroRotator, Saved.Location);
		AFelledTree* Felled = GetWorld()->SpawnActorDeferred<AFelledTree>(AFelledTree::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Felled->Setup(Tree, Saved.FallYaw, -1.0e6, Saved.OwnerKey, Saved.OwnerName, Saved.LogsLeft);
		Felled->FinishSpawning(At);
	}
	TArray<ACart*> Carts;
	for (const FSavedCart& Saved : WorldSave->Carts)
	{
		ACart* Cart = GetWorld()->SpawnActorDeferred<ACart>(ACart::StaticClass(), Saved.Transform);
		Cart->Setup(Saved.OwnerKey, Saved.OwnerName, static_cast<ECartKind>(Saved.Kind));
		for (int32 i = 0; i < Saved.Wood.Num() && i < Cart->EditWood().Num(); ++i)
		{
			Cart->EditWood()[i] = Saved.Wood[i];
		}
		Cart->FinishSpawning(Saved.Transform);
		Carts.Add(Cart);
		Cart->SetEngaged(Saved.bEngaged);
	}
	TArray<AStorageActor*> Storages;
	for (const FSavedStorage& Saved : WorldSave->Storages)
	{
		AStorageActor* Storage = GetWorld()->SpawnActorDeferred<AStorageActor>(AStorageActor::StaticClass(), Saved.Transform);
		Storage->Setup(static_cast<EStorageKind>(Saved.Kind), Saved.OwnerKey, Saved.OwnerName);
		if (Saved.SlotTimes.Num() > 0)
		{
			Storage->SetSlotTimes(Saved.SlotTimes);
		}
		Storage->SetExpiresAt(Saved.ExpiresAt);
		Storage->SetLevel(Saved.Level);
		Storage->SetBrewState(Saved.Temperature, 0.f);
		if (Saved.Water > 0.f)
		{
			Storage->SetLiquid(Saved.Liquid.IsNone() ? ErewhonApothecary::Water : Saved.Liquid, Saved.Water);
		}
		// Une étagère d'avant les cinq emplacements par étage : la pile de chaque étage va au premier emplacement.
		const bool bOldShelf = Saved.Kind == static_cast<uint8>(EStorageKind::Shelf) && Saved.Slots.Num() == ErewhonApothecary::ShelfRows;
		for (int32 i = 0; i < Saved.Slots.Num() && i < Storage->EditSlots().Num(); ++i)
		{
			const int32 Into = bOldShelf ? i * ErewhonApothecary::ShelfSlotsPerRow : i;
			if (Storage->EditSlots().IsValidIndex(Into))
			{
				Storage->EditSlots()[Into] = Saved.Slots[i];
			}
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
	// Les bhuraks, leur harnachement et le contenu de leurs sacoches.
	const int32 BagSize = ErewhonPlaceables::Capacity(EStorageKind::Saddlebag);
	for (const FSavedBhurak& Saved : WorldSave->Bhuraks)
	{
		ABhurak* Bhurak = SpawnBhurak(Saved.Transform, Saved.OwnerKey, Saved.OwnerName, Saved.Coat);
		Bhurak->SetHarness(Saved.bHarness);
		for (int32 Slot = 0; Slot < ABhurak::GearSlots && Slot < Saved.Gear.Num(); ++Slot)
		{
			if (Saved.Gear[Slot].IsNone())
			{
				continue;
			}
			AStorageActor* Bag = nullptr;
			if (Saved.Gear[Slot] == ABhurak::SaddlebagItem)
			{
				Bag = SpawnStorage(EStorageKind::Saddlebag, Bhurak->GearTransform(Slot) * Bhurak->GetActorTransform(), Saved.OwnerKey, Saved.OwnerName);
				for (int32 i = 0; i < BagSize && Saved.BagSlots.IsValidIndex(Slot * BagSize + i) && i < Bag->EditSlots().Num(); ++i)
				{
					Bag->EditSlots()[i] = Saved.BagSlots[Slot * BagSize + i];
				}
				Bag->Commit();
			}
			Bhurak->SetGear(Slot, Saved.Gear[Slot], Bag);
		}
	}
}

#undef LOCTEXT_NAMESPACE
