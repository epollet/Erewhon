#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "World/SectorTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "ErewhonGameState.generated.h"

class AErewhonGameState;

/** Une plante récoltée, qui repoussera après RegrowSeconds. */
USTRUCT()
struct FHarvestEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FIntPoint Sector = FIntPoint::ZeroValue;

	UPROPERTY()
	int32 PlantIndex = INDEX_NONE;

	/** Horloge du monde au moment de la récolte (s). */
	UPROPERTY()
	double HarvestTime = 0.0;

	void PostReplicatedAdd(const struct FHarvestArray& Array);
	void PreReplicatedRemove(const struct FHarvestArray& Array);
};

USTRUCT()
struct FHarvestArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FHarvestEntry> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<AErewhonGameState> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FHarvestEntry, FHarvestArray>(Items, DeltaParams, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FHarvestArray> : public TStructOpsTypeTraitsBase2<FHarvestArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Propriétaire d'un secteur acheté. */
USTRUCT()
struct FSectorOwnerEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FIntPoint Sector = FIntPoint::ZeroValue;

	/** Identifiant persistant du joueur (voir AErewhonPlayerState::PlayerKey). */
	UPROPERTY()
	FString OwnerKey;

	UPROPERTY()
	FString OwnerName;

	/** Améliorations achetées : un chemin relie la route voisine, un bit par côté (voir ErewhonSector::Sides). */
	UPROPERTY()
	uint8 RoadLinks = 0;

	/** Chemins d'accès pavés (mêmes bits), une fois le village pavé. */
	UPROPERTY()
	uint8 PavedLinks = 0;

	/** Bâtiments construits sur le secteur (bits de ESectorBuilding, voir World/Apothecary.h). */
	UPROPERTY()
	uint8 Buildings = 0;

	void PostReplicatedAdd(const struct FSectorOwnerArray& Array);
	void PostReplicatedChange(const struct FSectorOwnerArray& Array);
	void PreReplicatedRemove(const struct FSectorOwnerArray& Array);
};

USTRUCT()
struct FSectorOwnerArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSectorOwnerEntry> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<AErewhonGameState> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FSectorOwnerEntry, FSectorOwnerArray>(Items, DeltaParams, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FSectorOwnerArray> : public TStructOpsTypeTraitsBase2<FSectorOwnerArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Une case de champ travaillée ; une case absente est de la terre grossière (voir World/Farm.h). */
USTRUCT()
struct FFieldCellEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FIntPoint Sector = FIntPoint::ZeroValue;

	/** Y · ErewhonFarm::CellsPerSide + X. */
	UPROPERTY()
	int32 Cell = INDEX_NONE;

	/** 0 : retournée ; 1 + n : semée de la culture n (ErewhonFarm::Crops). */
	UPROPERTY()
	uint8 Crop = 0;

	/** Horloge du monde au semis (s). */
	UPROPERTY()
	double SownAt = 0.0;

	void PostReplicatedAdd(const struct FFieldArray& Array);
	void PostReplicatedChange(const struct FFieldArray& Array);
	void PreReplicatedRemove(const struct FFieldArray& Array);
};

USTRUCT()
struct FFieldArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FFieldCellEntry> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<AErewhonGameState> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FFieldCellEntry, FFieldArray>(Items, DeltaParams, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FFieldArray> : public TStructOpsTypeTraitsBase2<FFieldArray>
{
	enum { WithNetDeltaSerializer = true };
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnFieldChanged, const FIntPoint& /*Sector*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPlantHarvestChanged, const FIntPoint& /*Sector*/, int32 /*PlantIndex*/, bool /*bHarvested*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSectorOwnerChanged, const FIntPoint& /*Sector*/);
DECLARE_MULTICAST_DELEGATE(FOnVillageChanged);
struct FItemDef;

/**
 * État partagé du monde : seed, horloge, propriétés et récoltes.
 * Le serveur fait autorité ; les clients reçoivent les deltas via les fast arrays.
 * Au BeginPlay, chaque machine instancie localement ses acteurs de rendu
 * (ciel, secteurs, cabane) — ils se déduisent tous de cet état.
 */
UCLASS()
class EREWHON_API AErewhonGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AErewhonGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	static AErewhonGameState* Get(const UObject* WorldContext);

	// ── Horloge ──

	/** Âge du monde (s), synchronisé entre toutes les machines. */
	double GetWorldClock() const;

	int32 GetWorldSeed() const { return WorldSeed; }

	// ── Récoltes ──

	bool IsHarvested(const FIntPoint& Sector, int32 PlantIndex) const;
	const TSet<int32>* GetHarvestedIn(const FIntPoint& Sector) const { return HarvestedBySector.Find(Sector); }

	// ── Propriété ──

	/** Clé du propriétaire, vide si le secteur est libre. */
	const FString* FindSectorOwnerKey(const FIntPoint& Sector) const;
	FString GetSectorOwnerName(const FIntPoint& Sector) const;
	TArray<FIntPoint> GetSectorsOwnedBy(const FString& PlayerKey) const;

	// ── Village ──

	bool HasVillageUpgrade(EVillageUpgrade Upgrade) const { return (VillageUpgrades & (1 << static_cast<int32>(Upgrade))) != 0; }
	int32 GetVillageUpgrades() const { return VillageUpgrades; }
	bool IsVillagePaved() const { return HasVillageUpgrade(EVillageUpgrade::PavedCenter) && HasVillageUpgrade(EVillageUpgrade::PavedVillage); }

	/** Le commerce qui vend et rachète cet objet (les vêtements restent au General Store tant que le tailleur n'est pas là). */
	EShopKind ShopFor(const FItemDef& Item) const;

	uint8 GetPavedLinks(const FIntPoint& Sector) const;
	uint8 GetBuildings(const FIntPoint& Sector) const;

	// ── Champs ──

	/** Les cases travaillées d'un secteur (par index de case), nul s'il n'y en a pas. */
	const TMap<int32, FFieldCellEntry>* GetFieldCells(const FIntPoint& Sector) const { return FieldsBySector.Find(Sector); }
	const FFieldCellEntry* FindFieldCell(const FIntPoint& Sector, int32 Cell) const;
	const TArray<FFieldCellEntry>& GetFieldEntries() const { return Fields.Items; }
	/** Serveur : une case retournée (Crop 0) ou semée (1 + culture). */
	void SetFieldCell(const FIntPoint& Sector, int32 Cell, uint8 Crop, double SownAt);
	/** Serveur : la case redevient terre grossière. */
	void ClearFieldCell(const FIntPoint& Sector, int32 Cell);
	/** Serveur : tous les champs du secteur redeviennent terre grossière (revente). */
	void ClearFieldSector(const FIntPoint& Sector);
	void NotifyField(const FFieldCellEntry& Entry, bool bPresent);
	FOnFieldChanged OnFieldChanged;

	// ── Serveur uniquement ──

	void InitializeWorld(int32 InSeed, double InWorldAge);

	/** Triche : fait avancer l'horloge du monde (astres, repousse des plantes). */
	void AdvanceClock(double Seconds) { WorldAgeAtStart += FMath::Max(0.0, Seconds); }
	void AddHarvest(const FIntPoint& Sector, int32 PlantIndex, double HarvestTime);
	void SetSectorOwner(const FIntPoint& Sector, const FString& OwnerKey, const FString& OwnerName, uint8 RoadLinks = 0, uint8 PavedLinks = 0, uint8 Buildings = 0);
	void AddBuilding(const FIntPoint& Sector, uint8 Building);
	void SetVillageUpgrades(int32 Upgrades);
	void AddVillageUpgrade(EVillageUpgrade Upgrade);
	void AddPavedLink(const FIntPoint& Sector, int32 Side);

	/** Côtés du secteur reliés à la route par un chemin (0 s'il n'a pas de propriétaire). */
	uint8 GetRoadLinks(const FIntPoint& Sector) const;
	void AddRoadLink(const FIntPoint& Sector, int32 Side);
	void ClearSectorOwner(const FIntPoint& Sector);
	const TArray<FHarvestEntry>& GetHarvestEntries() const { return Harvests.Items; }
	const TArray<FSectorOwnerEntry>& GetOwnerEntries() const { return SectorOwners.Items; }

	FOnPlantHarvestChanged OnPlantHarvestChanged;
	FOnSectorOwnerChanged OnSectorOwnerChanged;
	FOnVillageChanged OnVillageChanged;

	// Appelés par les fast arrays (et directement côté serveur).
	void NotifyHarvest(const FHarvestEntry& Entry, bool bHarvested);
	void NotifyOwner(const FSectorOwnerEntry& Entry, bool bOwned);

private:
	void PruneHarvests();
	void SpawnLocalWorldActors();
	void SpawnCabin(EShopKind Kind);
	/** Le village a changé : le tailleur s'installe, l'habillage de la place se refait. */
	void ApplyVillage();

	UFUNCTION()
	void OnRep_Village();

	UPROPERTY(ReplicatedUsing = OnRep_Village)
	int32 VillageUpgrades = 0;

	UPROPERTY(Transient) TObjectPtr<AActor> VillageDecor;
	bool bTailorSpawned = false;

	UPROPERTY(Replicated)
	int32 WorldSeed = 0;

	/** Âge du monde au lancement du serveur ; l'horloge en découle. */
	UPROPERTY(Replicated)
	double WorldAgeAtStart = 0.0;

	UPROPERTY(Replicated)
	FHarvestArray Harvests;

	UPROPERTY(Replicated)
	FSectorOwnerArray SectorOwners;

	UPROPERTY(Replicated)
	FFieldArray Fields;

	TMap<FIntPoint, TMap<int32, FFieldCellEntry>> FieldsBySector;

	/** Caches locaux, tenus à jour par les notifications des fast arrays. */
	TMap<FIntPoint, TSet<int32>> HarvestedBySector;
	TMap<FIntPoint, FSectorOwnerEntry> OwnerBySector;

	FTimerHandle PruneTimer;
	bool bLocalActorsSpawned = false;
};
