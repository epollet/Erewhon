#include "Core/ErewhonGameState.h"

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

// ── AErewhonGameState ────────────────────────────────────────────────────────

AErewhonGameState::AErewhonGameState()
{
	Harvests.Owner = this;
	SectorOwners.Owner = this;
}

void AErewhonGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AErewhonGameState, WorldSeed);
	DOREPLIFETIME(AErewhonGameState, WorldAgeAtStart);
	DOREPLIFETIME(AErewhonGameState, Harvests);
	DOREPLIFETIME(AErewhonGameState, SectorOwners);
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
	// Les commerces de la place ; le tailleur ne vient qu'une fois sa boutique construite.
	SpawnCabin(EShopKind::GeneralStore);
	SpawnCabin(EShopKind::SectorManager);
	ApplyVillage();
	World->SpawnActor<AFountain>(Params);   // la fontaine de la place : elle arrête aussi le joueur sur le serveur
	if (GetNetMode() != NM_DedicatedServer)
	{
		World->SpawnActor<ACelestialSystem>(Params);
		World->SpawnActor<AWildlife>(Params);
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
	if (HasVillageUpgrade(EVillageUpgrade::Tailor) && !bTailorSpawned)
	{
		bTailorSpawned = true;
		SpawnCabin(EShopKind::Tailor);
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
	return Shop == EShopKind::Tailor && !HasVillageUpgrade(EVillageUpgrade::Tailor) ? EShopKind::GeneralStore : Shop;
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
	const double Regrow = UErewhonSettings::Get().RegrowSeconds;
	bool bChanged = false;
	for (int32 i = Harvests.Items.Num() - 1; i >= 0; --i)
	{
		if (Now - Harvests.Items[i].HarvestTime >= Regrow)
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
