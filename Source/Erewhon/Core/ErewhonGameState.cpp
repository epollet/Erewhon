#include "Core/ErewhonGameState.h"

#include "World/Fauna.h"
#include "Sky/WeatherEffects.h"
#include "World/Villagers.h"
#include "World/Forestry.h"
#include "Core/ErewhonSettings.h"
#include "Core/ErewhonPlayerState.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Sky/CelestialSystem.h"
#include "TimerManager.h"
#include "World/SectorManager.h"
#include "World/MusicPlayer.h"
#include "World/SpawnCabin.h"
#include "World/Village.h"
#include "World/VillageNetwork.h"
#include "Items/ItemTypes.h"
#include "World/SpawnLayout.h"
#include "World/Wildlife.h"
#include "Core/ErewhonUserSettings.h"
#include "GameFramework/PlayerController.h"

// ── Fast array callbacks (clients) ───────────────────────────────────────────

void FHarvestEntry::PostReplicatedAdd(const FHarvestArray& Array)
{
	if (Array.Owner)
	{
		Array.Owner->NotifyHarvest(*this, true);
	}
}

void FHarvestEntry::PreReplicatedRemove(const FHarvestArray& Array)
{
	if (Array.Owner)
	{
		Array.Owner->NotifyHarvest(*this, false);
	}
}

void FSectorOwnerEntry::PostReplicatedAdd(const FSectorOwnerArray& Array)
{
	if (Array.Owner)
	{
		Array.Owner->NotifyOwner(*this, true);
	}
}

void FSectorOwnerEntry::PostReplicatedChange(const FSectorOwnerArray& Array)
{
	if (Array.Owner)
	{
		Array.Owner->NotifyOwner(*this, true);
	}
}

void FSectorOwnerEntry::PreReplicatedRemove(const FSectorOwnerArray& Array)
{
	if (Array.Owner)
	{
		Array.Owner->NotifyOwner(*this, false);
	}
}

void FFieldCellEntry::PostReplicatedAdd(const FFieldArray& Array)
{
	if (Array.Owner)
	{
		Array.Owner->NotifyField(*this, true);
	}
}

void FFieldCellEntry::PostReplicatedChange(const FFieldArray& Array)
{
	if (Array.Owner)
	{
		Array.Owner->NotifyField(*this, true);
	}
}

void FFieldCellEntry::PreReplicatedRemove(const FFieldArray& Array)
{
	if (Array.Owner)
	{
		Array.Owner->NotifyField(*this, false);
	}
}

// ── AErewhonGameState ────────────────────────────────────────────────────────

AErewhonGameState::AErewhonGameState()
{
	Harvests.Owner = this;
	SectorOwners.Owner = this;
	Fields.Owner = this;
}

void AErewhonGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AErewhonGameState, WorldSeed);
	DOREPLIFETIME(AErewhonGameState, ForcedWeather);
	DOREPLIFETIME(AErewhonGameState, WorldAgeAtStart);
	DOREPLIFETIME(AErewhonGameState, Harvests);
	DOREPLIFETIME(AErewhonGameState, SectorOwners);
	DOREPLIFETIME(AErewhonGameState, Fields);
	DOREPLIFETIME(AErewhonGameState, VillageUpgrades);
}

AErewhonGameState* AErewhonGameState::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetGameState<AErewhonGameState>() : nullptr;
}

void AErewhonGameState::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(PruneTimer, this, &AErewhonGameState::PruneHarvests, 1.f, true);
	}
	SpawnLocalWorldActors();
}

void AErewhonGameState::SpawnLocalWorldActors()
{
	if (bLocalActorsSpawned)
	{
		return;
	}
	bLocalActorsSpawned = true;

	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Le serveur dédié a besoin du sol (collisions) mais pas du ciel.
	World->SpawnActor<ASectorManager>(Params);
	// Les villages : celui de départ tout de suite (le premier pawn y apparaît), puis ceux dont on approche.
	StreamVillages();
	GetWorldTimerManager().SetTimer(VillageStreamTimer, this, &AErewhonGameState::StreamVillages, 0.5f, true);
	if (GetNetMode() != NM_DedicatedServer)
	{
		World->SpawnActor<ACelestialSystem>(Params);
		World->SpawnActor<AWeatherEffects>(Params);   // pluie, éclairs, vent qui emporte feuilles et brins
		World->SpawnActor<AWildlife>(Params);
		World->SpawnActor<AFaunaManager>(Params);   // renards, et la faune au sol
		World->SpawnActor<AMusicPlayer>(Params);
	}
}

void AErewhonGameState::StreamVillages()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// La graine a changé (un client qui la reçoit après coup) : les villages ne sont plus au même endroit.
	if (StreamedSeed != ErewhonVillages::GetWorldSeed())
	{
		TArray<FIntPoint> Loaded;
		LocalVillages.GetKeys(Loaded);
		for (const FIntPoint& Village : Loaded)
		{
			DespawnVillage(Village);
		}
		StreamedSeed = ErewhonVillages::GetWorldSeed();
	}

	// Jusqu'à l'anneau lointain des secteurs (vue + 4) et un de plus : les toits se voient de loin, comme
	// au village de départ. Déchargés deux secteurs plus loin, pour ne pas les refaire à chaque pas.
	const UErewhonUserSettings* User = UErewhonUserSettings::Get();
	const int32 ViewDistance = User && GetNetMode() != NM_DedicatedServer ? User->ViewDistanceSectors : UErewhonSettings::Get().SectorRenderDistance;
	const int32 LoadRadius = ViewDistance + 5;
	const int32 KeepRadius = LoadRadius + 2;

	// Les joueurs de cette machine ; le serveur, lui, tient aussi les cabanes de tous les autres (collisions).
	TArray<FIntPoint> Focus;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (Pawn && (HasAuthority() || Controller->IsLocalController()))
		{
			Focus.Add(ErewhonSector::FromWorld(Pawn->GetActorLocation()));
		}
	}
	if (Focus.IsEmpty())
	{
		// En attendant un pawn : le village de départ, prêt à accueillir le joueur.
		Focus.Add(ErewhonVillages::CenterOf(ErewhonVillages::StartVillage));
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
		if (!LocalVillages.Contains(Village))
		{
			SpawnVillage(Village);
		}
	}
	TArray<FIntPoint> Far;
	for (const TPair<FIntPoint, FLocalVillage>& Pair : LocalVillages)
	{
		const FIntPoint Center = ErewhonVillages::CenterOf(Pair.Key);
		const bool bKept = Focus.ContainsByPredicate([&Center, KeepRadius](const FIntPoint& Sector)
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
		DespawnVillage(Village);
	}
}

void AErewhonGameState::SpawnVillage(const FIntPoint& Village)
{
	FLocalVillage& Local = LocalVillages.Add(Village);
	// Les commerces de la place ; le tailleur, l'atelier et l'éleveur ne viennent qu'une fois construits (ApplyVillage).
	SpawnCabin(Village, EShopKind::GeneralStore);
	SpawnCabin(Village, EShopKind::SectorManager);
	// Les maisons des habitants, au bord des routes.
	for (int32 House = 0; House < ErewhonVillagers::HouseCount; ++House)
	{
		const FTransform At = ErewhonVillagers::HouseTransform(Village, House);
		ASpawnCabin* Cabin = GetWorld()->SpawnActorDeferred<ASpawnCabin>(ASpawnCabin::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Cabin->Kind = EShopKind::House;
		Cabin->HouseIndex = House;
		Cabin->Village = Village;
		Cabin->FinishSpawning(At);
		Local.Houses.Add(Cabin);
	}
	// La fontaine de la place : elle arrête aussi le joueur sur le serveur.
	const FTransform FountainAt = ErewhonVillage::FountainTransform(Village);
	AFountain* Fountain = GetWorld()->SpawnActorDeferred<AFountain>(AFountain::StaticClass(), FountainAt, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Fountain->Village = Village;
	Fountain->FinishSpawning(FountainAt);
	Local.Fountain = Fountain;
	ApplyVillage(Village);
}

void AErewhonGameState::DespawnVillage(const FIntPoint& Village)
{
	FLocalVillage Local;
	if (!LocalVillages.RemoveAndCopyValue(Village, Local))
	{
		return;
	}
	TArray<TWeakObjectPtr<AActor>> Actors;
	Local.Shops.GenerateValueArray(Actors);
	Actors.Append(Local.Houses);
	Actors.Add(Local.Fountain);
	Actors.Add(Local.Decor);
	for (const TWeakObjectPtr<AActor>& Actor : Actors)
	{
		if (Actor.IsValid())
		{
			Actor->Destroy();
		}
	}
}

void AErewhonGameState::SpawnCabin(const FIntPoint& Village, EShopKind Kind)
{
	FLocalVillage* Local = LocalVillages.Find(Village);
	if (!Local)
	{
		return;
	}
	const FTransform At = ErewhonSpawn::CabinTransform(Village, Kind);
	ASpawnCabin* Cabin = GetWorld()->SpawnActorDeferred<ASpawnCabin>(ASpawnCabin::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Cabin->Kind = Kind;
	Cabin->Village = Village;
	Cabin->FinishSpawning(At);
	Local->Shops.Add(Kind, Cabin);
}

void AErewhonGameState::ApplyVillage(const FIntPoint& Village)
{
	FLocalVillage* Local = LocalVillages.Find(Village);
	if (!Local)
	{
		return;   // pas chargé ici : tout se construira quand on en approchera
	}
	for (const EShopKind Kind : { EShopKind::Tailor, EShopKind::Workshop, EShopKind::Breeder })
	{
		if (IsShopBuilt(Village, Kind) && !Local->Shops.Contains(Kind))
		{
			SpawnCabin(Village, Kind);
		}
	}
	Local = LocalVillages.Find(Village);
	if (Local->Decor.IsValid())
	{
		Local->Decor->Destroy();
	}
	AVillageDecor* Decor = GetWorld()->SpawnActorDeferred<AVillageDecor>(AVillageDecor::StaticClass(), ErewhonVillages::VillageFrame(Village),
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Decor->Village = Village;
	Decor->FinishSpawning(ErewhonVillages::VillageFrame(Village));
	Local->Decor = Decor;
}

int32 AErewhonGameState::GetVillageUpgrades(const FIntPoint& Village) const
{
	const FVillageUpgradesEntry* Entry = VillageUpgrades.FindByPredicate([&Village](const FVillageUpgradesEntry& E) { return E.Village == Village; });
	return Entry ? Entry->Upgrades : 0;
}

void AErewhonGameState::SetVillageUpgrades(const FIntPoint& Village, int32 Upgrades)
{
	check(HasAuthority());
	const TArray<FVillageUpgradesEntry> Previous = VillageUpgrades;
	FVillageUpgradesEntry* Entry = VillageUpgrades.FindByPredicate([&Village](const FVillageUpgradesEntry& E) { return E.Village == Village; });
	if (!Entry)
	{
		Entry = &VillageUpgrades.AddDefaulted_GetRef();
		Entry->Village = Village;
	}
	Entry->Upgrades = Upgrades;
	OnRep_Village(Previous);   // l'hôte ne reçoit pas d'OnRep
}

void AErewhonGameState::AddVillageUpgrade(const FIntPoint& Village, EVillageUpgrade Upgrade)
{
	SetVillageUpgrades(Village, GetVillageUpgrades(Village) | (1 << static_cast<int32>(Upgrade)));
}

void AErewhonGameState::OnRep_WorldSeed()
{
	ErewhonVillages::SetWorldSeed(WorldSeed);
}

void AErewhonGameState::OnRep_Village(const TArray<FVillageUpgradesEntry>& Previous)
{
	// Seuls les villages dont les améliorations ont changé se refont (et leurs secteurs, voir ASectorManager).
	TSet<FIntPoint> Changed;
	for (const FVillageUpgradesEntry& Entry : VillageUpgrades)
	{
		const FVillageUpgradesEntry* Before = Previous.FindByPredicate([&Entry](const FVillageUpgradesEntry& E) { return E.Village == Entry.Village; });
		if (!Before || Before->Upgrades != Entry.Upgrades)
		{
			Changed.Add(Entry.Village);
		}
	}
	for (const FVillageUpgradesEntry& Entry : Previous)
	{
		if (GetVillageUpgrades(Entry.Village) != Entry.Upgrades)
		{
			Changed.Add(Entry.Village);
		}
	}
	for (const FIntPoint& Village : Changed)
	{
		// D'abord les plans des secteurs (ASectorManager les oublie), puis l'habillage qui les lit.
		OnVillageChanged.Broadcast(Village);
		ApplyVillage(Village);
	}
}

EShopKind AErewhonGameState::ShopFor(const FIntPoint& Village, const FItemDef& Item) const
{
	const EShopKind Shop = Item.ShopFor();
	// Les teintures, et la charrette, au General Store tant que leur boutique n'est pas bâtie dans ce village ;
	// les vêtements, bhuraks et outils tractés attendent le tailleur, l'éleveur et l'atelier.
	if (!IsShopBuilt(Village, Shop) && ((Shop == EShopKind::Tailor && !Item.IsClothing()) || Item.Id == TEXT("cart")))
	{
		return EShopKind::GeneralStore;
	}
	return Shop;
}

#define LOCTEXT_NAMESPACE "ErewhonGameState"

FText AErewhonGameState::WhyNotOffered(const FIntPoint& Village, const FItemDef& Item, EShopKind Shop, const AErewhonPlayerState* Player) const
{
	if (!Player || Item.BuyPrice <= 0 || ShopFor(Village, Item) != Shop || !IsShopBuilt(Village, Shop))
	{
		return LOCTEXT("NotSold", "Le marchand ne vend pas cet objet.");
	}
	if (Item.bApothecaryOnly && !Player->IsApothecary())
	{
		return LOCTEXT("ApothecaryOnlyItem", "Ce n'est vendu qu'aux apothicaires (la classe se choisit à la mairie).");
	}
	if (Item.bFarmerOnly && !Player->IsFarmer())
	{
		return LOCTEXT("FarmerOnlyItem", "Ce n'est vendu qu'aux fermiers (la classe se choisit à la mairie).");
	}
	if (Item.bForesterOnly && !Player->IsForester())
	{
		return LOCTEXT("ForesterOnlyItem", "Ce n'est vendu qu'aux forestiers (la classe se choisit à la mairie).");
	}
	// Le sac se porte sur le dos, les outils ne s'achètent qu'une fois.
	if (Item.IsBackpack() ? Player->GetBackpack()->IsOwned() : Item.IsUnique() && Player->GetInventory()->Owns(Item.Id))
	{
		return FText::Format(LOCTEXT("AlreadyHave", "Vous possédez déjà : {0}."), Item.Name);
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE

bool AErewhonGameState::BuysBack(const FIntPoint& Village, const FItemDef& Item, EShopKind Shop) const
{
	return ShopFor(Village, Item) == Shop && IsShopBuilt(Village, Shop) && Item.ResalePrice(UErewhonSettings::Get().ResaleRatio) > 0;
}

bool AErewhonGameState::IsShopBuilt(const FIntPoint& Village, EShopKind Kind) const
{
	switch (Kind)
	{
	case EShopKind::Tailor:   return HasVillageUpgrade(Village, EVillageUpgrade::Tailor);
	case EShopKind::Workshop: return HasVillageUpgrade(Village, EVillageUpgrade::Workshop);
	case EShopKind::Breeder:  return HasVillageUpgrade(Village, EVillageUpgrade::Breeder);
	default:                  return true;
	}
}

uint8 AErewhonGameState::GetPavedLinks(const FIntPoint& Sector) const
{
	const FSectorOwnerEntry* Entry = OwnerBySector.Find(Sector);
	return Entry ? Entry->PavedLinks : 0;
}

void AErewhonGameState::AddPavedLink(const FIntPoint& Sector, int32 Side)
{
	check(HasAuthority());
	FSectorOwnerEntry* Entry = SectorOwners.Items.FindByPredicate([&](const FSectorOwnerEntry& E) { return E.Sector == Sector; });
	if (Entry && Side >= 0 && Side < 4)
	{
		Entry->PavedLinks |= 1 << Side;
		SectorOwners.MarkItemDirty(*Entry);
		NotifyOwner(*Entry, true);
	}
}

double AErewhonGameState::GetWorldClock() const
{
	return WorldAgeAtStart + GetServerWorldTimeSeconds();
}

ErewhonWeather::FState AErewhonGameState::SampleWeather() const
{
	TOptional<ErewhonWeather::EKind> Forced;
	if (ForcedWeather != 0)
	{
		Forced = static_cast<ErewhonWeather::EKind>(ForcedWeather - 1);
	}
	return ErewhonWeather::Sample(GetWorldClock(), WorldSeed, Forced);
}

void AErewhonGameState::SetForcedWeather(TOptional<ErewhonWeather::EKind> Kind)
{
	ForcedWeather = Kind.IsSet() ? static_cast<uint8>(Kind.GetValue()) + 1 : 0;
}

bool AErewhonGameState::IsHarvested(const FIntPoint& Sector, int32 PlantIndex) const
{
	const TSet<int32>* Set = HarvestedBySector.Find(Sector);
	return Set && Set->Contains(PlantIndex);
}

const FString* AErewhonGameState::FindSectorOwnerKey(const FIntPoint& Sector) const
{
	const FSectorOwnerEntry* Entry = OwnerBySector.Find(Sector);
	return Entry ? &Entry->OwnerKey : nullptr;
}

FString AErewhonGameState::GetSectorOwnerName(const FIntPoint& Sector) const
{
	const FSectorOwnerEntry* Entry = OwnerBySector.Find(Sector);
	return Entry ? Entry->OwnerName : FString();
}

TArray<FIntPoint> AErewhonGameState::GetSectorsOwnedBy(const FString& PlayerKey) const
{
	TArray<FIntPoint> Out;
	for (const auto& Pair : OwnerBySector)
	{
		if (Pair.Value.OwnerKey == PlayerKey)
		{
			Out.Add(Pair.Key);
		}
	}
	Out.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	return Out;
}

// ── Territoire des villages ──────────────────────────────────────────────────

FIntPoint AErewhonGameState::GetSectorVillage(const FIntPoint& Sector) const
{
	return ErewhonVillages::VillageOf(Sector);
}

bool AErewhonGameState::IsVillageTerritory(const FIntPoint& Sector, const FIntPoint& Village) const
{
	return Sector == ErewhonVillages::CenterOf(Village) || (OwnerBySector.Contains(Sector) && ErewhonVillages::VillageOf(Sector) == Village);
}

bool AErewhonGameState::BordersVillageTerritory(const FIntPoint& Sector, const FIntPoint& Village) const
{
	// La place a des routes sur ses quatre côtés : on s'y raccroche par ses coins.
	const FIntPoint FromPlaza = Sector - ErewhonVillages::CenterOf(Village);
	if (FMath::Max(FMath::Abs(FromPlaza.X), FMath::Abs(FromPlaza.Y)) == 1)
	{
		return true;
	}
	for (const FIntPoint& Side : ErewhonSector::Sides)
	{
		if (OwnerBySector.Contains(Sector + Side) && ErewhonVillages::VillageOf(Sector + Side) == Village)
		{
			return true;
		}
	}
	return false;
}

bool AErewhonGameState::KeepsVillageConnected(const FIntPoint& Removed, const FIntPoint& Village) const
{
	TSet<FIntPoint> Remaining;
	for (const auto& Pair : OwnerBySector)
	{
		if (Pair.Key != Removed && ErewhonVillages::VillageOf(Pair.Key) == Village)
		{
			Remaining.Add(Pair.Key);
		}
	}
	// On part des secteurs qui touchent la place, puis de proche en proche par les côtés.
	TArray<FIntPoint> Open;
	TSet<FIntPoint> Reached;
	const FIntPoint Plaza = ErewhonVillages::CenterOf(Village);
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			const FIntPoint Start = Plaza + FIntPoint(DX, DY);
			if (Remaining.Contains(Start))
			{
				Reached.Add(Start);
				Open.Add(Start);
			}
		}
	}
	while (Open.Num() > 0)
	{
		const FIntPoint Current = Open.Pop(EAllowShrinking::No);
		for (const FIntPoint& Side : ErewhonSector::Sides)
		{
			const FIntPoint Next = Current + Side;
			if (Remaining.Contains(Next) && !Reached.Contains(Next))
			{
				Reached.Add(Next);
				Open.Add(Next);
			}
		}
	}
	return Reached.Num() == Remaining.Num();
}

#define LOCTEXT_NAMESPACE "ErewhonGameState"

FText AErewhonGameState::WhyNotBuyable(const FIntPoint& Sector, const FIntPoint& Village, const AErewhonPlayerState* Player) const
{
	if (!Player)
	{
		return LOCTEXT("NotForSale", "Ce secteur n'est pas à vendre.");
	}
	if (!ErewhonSector::IsPurchasable(Sector))
	{
		return ErewhonVillages::IsCenter(Sector)
			? LOCTEXT("PlazaNotForSale", "La place du village, avec ses commerces et la mairie : c'est public, pas à vendre !")
			: ErewhonVillages::IsRoad(Sector)
			? LOCTEXT("RoadNotForSale", "Une route : c'est public. Achetez un secteur qui la borde, un chemin vous y mènera.")
			: LOCTEXT("NotForSale", "Ce secteur n'est pas à vendre.");
	}
	if (OwnerBySector.Contains(Sector))
	{
		return LOCTEXT("AlreadyOwned", "Ce secteur a déjà un propriétaire.");
	}
	// Chaque mairie vend les secteurs plus proches de sa place que de toute autre : les villages ne se chevauchent pas.
	const FIntPoint Home = ErewhonVillages::VillageOf(Sector);
	if (Home != Village)
	{
		return FText::Format(LOCTEXT("OtherMairie", "Ce secteur dépend de la mairie de {0}."), ErewhonVillages::VillageName(Home));
	}
	if (!BordersVillageTerritory(Sector, Village))
	{
		return LOCTEXT("NotConnected", "Trop à l'écart : un secteur s'achète contre la place ou contre un secteur déjà acheté du village, pour ne pas rester isolé.");
	}
	return FText::GetEmpty();
}

FText AErewhonGameState::WhyNotSellable(const FIntPoint& Sector, const FIntPoint& Village, const AErewhonPlayerState* Player) const
{
	const FSectorOwnerEntry* Entry = OwnerBySector.Find(Sector);
	if (!Player || !Entry || Entry->OwnerKey != Player->GetPlayerKey())
	{
		return LOCTEXT("NotYours", "Ce secteur ne vous appartient pas.");
	}
	const FIntPoint Home = ErewhonVillages::VillageOf(Sector);
	if (Home != Village)
	{
		return FText::Format(LOCTEXT("SellAtOtherMairie", "Ce secteur se revend à la mairie de {0}."), ErewhonVillages::VillageName(Home));
	}
	if (!KeepsVillageConnected(Sector, Village))
	{
		return LOCTEXT("WouldIsolate", "Le revendre couperait d'autres secteurs du village de sa place : ils doivent y rester reliés.");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE

void AErewhonGameState::InitializeWorld(int32 InSeed, double InWorldAge)
{
	check(HasAuthority());
	// Le temps serveur part de 0 au lancement : on retranche ce qui s'est déjà écoulé.
	WorldSeed = InSeed;
	ErewhonVillages::SetWorldSeed(InSeed);
	WorldAgeAtStart = InWorldAge - GetServerWorldTimeSeconds();
}

void AErewhonGameState::AddHarvest(const FIntPoint& Sector, int32 PlantIndex, double HarvestTime)
{
	check(HasAuthority());
	FHarvestEntry& Entry = Harvests.Items.AddDefaulted_GetRef();
	Entry.Sector = Sector;
	Entry.PlantIndex = PlantIndex;
	Entry.HarvestTime = HarvestTime;
	Harvests.MarkItemDirty(Entry);
	NotifyHarvest(Entry, true);
}

void AErewhonGameState::PruneHarvests()
{
	const double Now = GetWorldClock();
	bool bChanged = false;
	for (int32 i = Harvests.Items.Num() - 1; i >= 0; --i)
	{
		if (Now - Harvests.Items[i].HarvestTime >= ErewhonForestry::RegrowSeconds(Harvests.Items[i].PlantIndex))
		{
			const FHarvestEntry Removed = Harvests.Items[i];
			Harvests.Items.RemoveAtSwap(i);
			NotifyHarvest(Removed, false);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		Harvests.MarkArrayDirty();
	}
}

uint8 AErewhonGameState::GetRoadLinks(const FIntPoint& Sector) const
{
	const FSectorOwnerEntry* Entry = OwnerBySector.Find(Sector);
	return Entry ? Entry->RoadLinks : 0;
}

uint8 AErewhonGameState::GetBuildings(const FIntPoint& Sector) const
{
	const FSectorOwnerEntry* Entry = OwnerBySector.Find(Sector);
	return Entry ? Entry->Buildings : 0;
}

void AErewhonGameState::AddBuilding(const FIntPoint& Sector, uint8 Building)
{
	check(HasAuthority());
	FSectorOwnerEntry* Entry = SectorOwners.Items.FindByPredicate([&](const FSectorOwnerEntry& E) { return E.Sector == Sector; });
	if (Entry)
	{
		Entry->Buildings |= Building;
		SectorOwners.MarkItemDirty(*Entry);
		NotifyOwner(*Entry, true);
	}
}

void AErewhonGameState::AddRoadLink(const FIntPoint& Sector, int32 Side)
{
	check(HasAuthority());
	FSectorOwnerEntry* Entry = SectorOwners.Items.FindByPredicate([&](const FSectorOwnerEntry& E) { return E.Sector == Sector; });
	if (Entry && Side >= 0 && Side < 4)
	{
		Entry->RoadLinks |= 1 << Side;
		SectorOwners.MarkItemDirty(*Entry);
		NotifyOwner(*Entry, true);
	}
}

void AErewhonGameState::SetSectorOwner(const FIntPoint& Sector, const FString& OwnerKey, const FString& OwnerName, uint8 RoadLinks, uint8 PavedLinks, uint8 Buildings)
{
	check(HasAuthority());
	FSectorOwnerEntry* Entry = SectorOwners.Items.FindByPredicate([&](const FSectorOwnerEntry& E) { return E.Sector == Sector; });
	if (!Entry)
	{
		Entry = &SectorOwners.Items.AddDefaulted_GetRef();
		Entry->Sector = Sector;
	}
	Entry->OwnerKey = OwnerKey;
	Entry->OwnerName = OwnerName;
	Entry->RoadLinks = RoadLinks;
	Entry->PavedLinks = PavedLinks;
	Entry->Buildings = Buildings;
	SectorOwners.MarkItemDirty(*Entry);
	NotifyOwner(*Entry, true);
}

void AErewhonGameState::ClearSectorOwner(const FIntPoint& Sector)
{
	check(HasAuthority());
	const int32 Index = SectorOwners.Items.IndexOfByPredicate([&](const FSectorOwnerEntry& E) { return E.Sector == Sector; });
	if (Index != INDEX_NONE)
	{
		const FSectorOwnerEntry Removed = SectorOwners.Items[Index];
		SectorOwners.Items.RemoveAtSwap(Index);
		SectorOwners.MarkArrayDirty();
		NotifyOwner(Removed, false);
	}
}

const FFieldCellEntry* AErewhonGameState::FindFieldCell(const FIntPoint& Sector, int32 Cell) const
{
	const TMap<int32, FFieldCellEntry>* Cells = FieldsBySector.Find(Sector);
	return Cells ? Cells->Find(Cell) : nullptr;
}

void AErewhonGameState::SetFieldCell(const FIntPoint& Sector, int32 Cell, uint8 Crop, double SownAt)
{
	check(HasAuthority());
	FFieldCellEntry* Entry = Fields.Items.FindByPredicate([&](const FFieldCellEntry& E) { return E.Cell == Cell && E.Sector == Sector; });
	if (!Entry)
	{
		Entry = &Fields.Items.AddDefaulted_GetRef();
		Entry->Sector = Sector;
		Entry->Cell = Cell;
	}
	Entry->Crop = Crop;
	Entry->SownAt = SownAt;
	Fields.MarkItemDirty(*Entry);
	NotifyField(*Entry, true);
}

void AErewhonGameState::ClearFieldCell(const FIntPoint& Sector, int32 Cell)
{
	check(HasAuthority());
	const int32 Index = Fields.Items.IndexOfByPredicate([&](const FFieldCellEntry& E) { return E.Cell == Cell && E.Sector == Sector; });
	if (Index != INDEX_NONE)
	{
		const FFieldCellEntry Removed = Fields.Items[Index];
		Fields.Items.RemoveAtSwap(Index);
		Fields.MarkArrayDirty();
		NotifyField(Removed, false);
	}
}

void AErewhonGameState::ClearFieldSector(const FIntPoint& Sector)
{
	check(HasAuthority());
	TArray<FFieldCellEntry> Removed;
	for (int32 i = Fields.Items.Num() - 1; i >= 0; --i)
	{
		if (Fields.Items[i].Sector == Sector)
		{
			Removed.Add(Fields.Items[i]);
			Fields.Items.RemoveAtSwap(i);
		}
	}
	if (Removed.Num() > 0)
	{
		Fields.MarkArrayDirty();
		for (const FFieldCellEntry& Entry : Removed)
		{
			NotifyField(Entry, false);
		}
	}
}

void AErewhonGameState::NotifyField(const FFieldCellEntry& Entry, bool bPresent)
{
	if (bPresent)
	{
		FieldsBySector.FindOrAdd(Entry.Sector).Add(Entry.Cell, Entry);
	}
	else if (TMap<int32, FFieldCellEntry>* Cells = FieldsBySector.Find(Entry.Sector))
	{
		Cells->Remove(Entry.Cell);
	}
	OnFieldChanged.Broadcast(Entry.Sector);
}

void AErewhonGameState::NotifyHarvest(const FHarvestEntry& Entry, bool bHarvested)
{
	if (bHarvested)
	{
		HarvestedBySector.FindOrAdd(Entry.Sector).Add(Entry.PlantIndex);
	}
	else if (TSet<int32>* Set = HarvestedBySector.Find(Entry.Sector))
	{
		Set->Remove(Entry.PlantIndex);
	}
	OnPlantHarvestChanged.Broadcast(Entry.Sector, Entry.PlantIndex, bHarvested);
}

void AErewhonGameState::NotifyOwner(const FSectorOwnerEntry& Entry, bool bOwned)
{
	if (bOwned)
	{
		OwnerBySector.Add(Entry.Sector, Entry);
	}
	else
	{
		OwnerBySector.Remove(Entry.Sector);
	}
	OnSectorOwnerChanged.Broadcast(Entry.Sector);
}
