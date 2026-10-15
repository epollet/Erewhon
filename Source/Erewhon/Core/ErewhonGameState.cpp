#include "Core/ErewhonGameState.h"

#include "World/Fauna.h"
#include "Sky/WeatherEffects.h"
#include "World/Villagers.h"
#include "World/Forestry.h"
#include "Core/ErewhonSettings.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Sky/CelestialSystem.h"
#include "TimerManager.h"
#include "World/SectorManager.h"
#include "World/MusicPlayer.h"
#include "World/SpawnCabin.h"
#include "World/Village.h"
#include "Items/ItemTypes.h"
#include "World/SpawnLayout.h"
#include "World/Wildlife.h"

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
	// Les commerces de la place ; le tailleur, l'atelier et l'éleveur ne viennent qu'une fois construits (ApplyVillage).
	SpawnCabin(EShopKind::GeneralStore);
	SpawnCabin(EShopKind::SectorManager);
	// Les maisons des habitants, au bord des routes.
	for (int32 House = 0; House < ErewhonVillagers::HouseCount; ++House)
	{
		const FTransform At = ErewhonVillagers::HouseTransform(House);
		ASpawnCabin* Cabin = GetWorld()->SpawnActorDeferred<ASpawnCabin>(ASpawnCabin::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Cabin->Kind = EShopKind::House;
		Cabin->HouseIndex = House;
		Cabin->FinishSpawning(At);
	}
	ApplyVillage();
	World->SpawnActor<AFountain>(Params);   // la fontaine de la place : elle arrête aussi le joueur sur le serveur
	if (GetNetMode() != NM_DedicatedServer)
	{
		World->SpawnActor<ACelestialSystem>(Params);
		World->SpawnActor<AWeatherEffects>(Params);   // pluie, éclairs, vent qui emporte feuilles et brins
		World->SpawnActor<AWildlife>(Params);
		World->SpawnActor<AFaunaManager>(Params);   // renards, et la faune au sol
		World->SpawnActor<AMusicPlayer>(Params);
	}
}

void AErewhonGameState::SpawnCabin(EShopKind Kind)
{
	ASpawnCabin* Cabin = GetWorld()->SpawnActorDeferred<ASpawnCabin>(ASpawnCabin::StaticClass(), ErewhonSpawn::CabinTransform(Kind),
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Cabin->Kind = Kind;
	Cabin->FinishSpawning(ErewhonSpawn::CabinTransform(Kind));
}

void AErewhonGameState::ApplyVillage()
{
	if (!bLocalActorsSpawned)
	{
		return;   // tout se construira avec les acteurs locaux
	}
	for (const EShopKind Kind : { EShopKind::Tailor, EShopKind::Workshop, EShopKind::Breeder })
	{
		if (IsShopBuilt(Kind) && !SpawnedShops.Contains(Kind))
		{
			SpawnedShops.Add(Kind);
			SpawnCabin(Kind);
		}
	}
	if (VillageDecor)
	{
		VillageDecor->Destroy();
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	VillageDecor = GetWorld()->SpawnActor<AVillageDecor>(Params);
}

void AErewhonGameState::SetVillageUpgrades(int32 Upgrades)
{
	check(HasAuthority());
	VillageUpgrades = Upgrades;
	OnRep_Village();
}

void AErewhonGameState::AddVillageUpgrade(EVillageUpgrade Upgrade)
{
	SetVillageUpgrades(VillageUpgrades | (1 << static_cast<int32>(Upgrade)));
}

void AErewhonGameState::OnRep_Village()
{
	ApplyVillage();
	OnVillageChanged.Broadcast();
}

EShopKind AErewhonGameState::ShopFor(const FItemDef& Item) const
{
	const EShopKind Shop = Item.ShopFor();
	// Les vêtements, et la charrette, au General Store tant que leur boutique n'est pas bâtie ;
	// bhuraks et outils tractés attendent l'éleveur et l'atelier.
	if (!IsShopBuilt(Shop) && (Shop == EShopKind::Tailor || Item.Id == TEXT("cart")))
	{
		return EShopKind::GeneralStore;
	}
	return Shop;
}

bool AErewhonGameState::IsShopBuilt(EShopKind Kind) const
{
	switch (Kind)
	{
	case EShopKind::Tailor:   return HasVillageUpgrade(EVillageUpgrade::Tailor);
	case EShopKind::Workshop: return HasVillageUpgrade(EVillageUpgrade::Workshop);
	case EShopKind::Breeder:  return HasVillageUpgrade(EVillageUpgrade::Breeder);
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

void AErewhonGameState::InitializeWorld(int32 InSeed, double InWorldAge)
{
	check(HasAuthority());
	// Le temps serveur part de 0 au lancement : on retranche ce qui s'est déjà écoulé.
	WorldSeed = InSeed;
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
