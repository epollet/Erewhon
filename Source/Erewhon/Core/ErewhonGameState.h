#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Sky/ErewhonWeather.h"
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
/** Les améliorations achetées à la mairie d'un village (un bit par EVillageUpgrade). */
USTRUCT()
struct FVillageUpgradesEntry
{
	GENERATED_BODY()

	/** Le village : sa cellule (voir ErewhonVillages), pas le secteur de sa place. */
	UPROPERTY()
	FIntPoint Village = FIntPoint::ZeroValue;

	UPROPERTY()
	int32 Upgrades = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSectorOwnerChanged, const FIntPoint& /*Sector*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVillageChanged, const FIntPoint& /*Village*/);
struct FItemDef;
class AErewhonPlayerState;

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

	// ── Météo ──

	/** Le temps qu'il fait, le même sur toutes les machines (forcé par la triche, sinon naturel). */
	ErewhonWeather::FState SampleWeather() const;
	bool IsWeatherForced() const { return ForcedWeather != 0; }
	/** Serveur : fixe le type de temps, ou rend la météo naturelle (Kind non défini). */
	void SetForcedWeather(TOptional<ErewhonWeather::EKind> Kind);

	// ── Récoltes ──

	bool IsHarvested(const FIntPoint& Sector, int32 PlantIndex) const;
	const TSet<int32>* GetHarvestedIn(const FIntPoint& Sector) const { return HarvestedBySector.Find(Sector); }

	// ── Propriété ──

	/** Clé du propriétaire, vide si le secteur est libre. */
	const FString* FindSectorOwnerKey(const FIntPoint& Sector) const;
	FString GetSectorOwnerName(const FIntPoint& Sector) const;
	TArray<FIntPoint> GetSectorsOwnedBy(const FString& PlayerKey) const;

	// ── Territoire des villages ──
	// Chaque secteur appartient pour toujours au même village (ErewhonVillages::VillageOf), qu'il soit libre
	// ou acheté ; seule sa mairie le vend. Un secteur acheté doit rester relié à la place de son village :
	// accolé (côté contre côté) à un secteur acheté du même village, ou touchant la place, coins compris
	// (ses quatre côtés sont des routes). Une seule source de vérité pour la carte du maire et le serveur.

	/** Le village du secteur, fixe (voir ErewhonVillages::VillageOf). */
	FIntPoint GetSectorVillage(const FIntPoint& Sector) const;
	/** Le territoire d'un village : sa place et les secteurs achetés à sa mairie. */
	bool IsVillageTerritory(const FIntPoint& Sector, const FIntPoint& Village) const;
	/** Ce secteur touche-t-il le territoire du village : la place, coins compris, ou un secteur du village par un côté ? */
	bool BordersVillageTerritory(const FIntPoint& Sector, const FIntPoint& Village) const;
	/**
	 * Pourquoi ce joueur ne peut acheter ce secteur à la mairie de ce village (vide : il le peut) : public,
	 * déjà à quelqu'un, du ressort d'une autre mairie, pas relié au village. Les pièces n'y comptent pas :
	 * la carte grise le bouton faute d'argent, le serveur refuse.
	 */
	FText WhyNotBuyable(const FIntPoint& Sector, const FIntPoint& Village, const AErewhonPlayerState* Player) const;
	bool IsBuyable(const FIntPoint& Sector, const FIntPoint& Village, const AErewhonPlayerState* Player) const { return WhyNotBuyable(Sector, Village, Player).IsEmpty(); }
	/** Pourquoi ce joueur ne peut revendre ce secteur à la mairie de ce village (vide : il le peut) : pas à lui, autre mairie, il couperait le village. */
	FText WhyNotSellable(const FIntPoint& Sector, const FIntPoint& Village, const AErewhonPlayerState* Player) const;
	bool IsSellable(const FIntPoint& Sector, const FIntPoint& Village, const AErewhonPlayerState* Player) const { return WhyNotSellable(Sector, Village, Player).IsEmpty(); }
	/** Sans ce secteur, les autres secteurs du village restent-ils tous reliés à sa place ? */
	bool KeepsVillageConnected(const FIntPoint& Removed, const FIntPoint& Village) const;

	// ── Village ──
	// Chaque village a ses améliorations, achetées à sa mairie (un village est désigné par sa cellule, voir
	// ErewhonVillages). Celui d'un joueur à un comptoir : ErewhonSpawn::VillageAt(sa position).

	int32 GetVillageUpgrades(const FIntPoint& Village) const;
	bool HasVillageUpgrade(const FIntPoint& Village, EVillageUpgrade Upgrade) const { return (GetVillageUpgrades(Village) & (1 << static_cast<int32>(Upgrade))) != 0; }
	bool IsVillagePaved(const FIntPoint& Village) const { return HasVillageUpgrade(Village, EVillageUpgrade::PavedCenter) && HasVillageUpgrade(Village, EVillageUpgrade::PavedVillage); }
	/** Les villages qui ont au moins une amélioration (pour la sauvegarde). */
	const TArray<FVillageUpgradesEntry>& GetVillageUpgradeEntries() const { return VillageUpgrades; }

	/**
	 * Le commerce de ce village qui vend et rachète cet objet (les vêtements chez le tailleur seulement, la charrette
	 * au General Store tant que l'atelier n'est pas là).
	 */
	EShopKind ShopFor(const FIntPoint& Village, const FItemDef& Item) const;
	/**
	 * Pourquoi ce commerce du village ne propose pas l'objet à ce joueur (vide : il le lui propose) : pas sa marchandise,
	 * boutique pas bâtie, réservé à une autre classe, objet unique déjà à lui. Seules les pièces n'y comptent pas :
	 * l'étal ne montre que ce qui est proposé (bouton grisé faute d'argent), le serveur refuse le reste.
	 */
	FText WhyNotOffered(const FIntPoint& Village, const FItemDef& Item, EShopKind Shop, const AErewhonPlayerState* Player) const;
	bool IsOfferedTo(const FIntPoint& Village, const FItemDef& Item, EShopKind Shop, const AErewhonPlayerState* Player) const { return WhyNotOffered(Village, Item, Shop, Player).IsEmpty(); }
	/** Ce commerce du village rachète-t-il cet objet ? */
	bool BuysBack(const FIntPoint& Village, const FItemDef& Item, EShopKind Shop) const;
	/** La boutique est-elle bâtie dans ce village ? Le tailleur, l'atelier et l'éleveur s'achètent comme améliorations du village. */
	bool IsShopBuilt(const FIntPoint& Village, EShopKind Kind) const;

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
	void SetVillageUpgrades(const FIntPoint& Village, int32 Upgrades);
	void AddVillageUpgrade(const FIntPoint& Village, EVillageUpgrade Upgrade);
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

	/**
	 * Les acteurs locaux des villages (cabanes et marchands, maisons, fontaine, habillage de la place), sur chaque
	 * machine : chargés quand un joueur approche (les siens sur un client, tous sur le serveur, qui a besoin des
	 * collisions), jusqu'à l'anneau lointain des secteurs pour qu'on voie les toits de loin ; déchargés plus loin.
	 */
	void StreamVillages();
	void SpawnVillage(const FIntPoint& Village);
	void DespawnVillage(const FIntPoint& Village);
	void SpawnCabin(const FIntPoint& Village, EShopKind Kind);
	/** Le village a changé : le tailleur s'installe, l'habillage de la place se refait (s'il est chargé ici). */
	void ApplyVillage(const FIntPoint& Village);

	UFUNCTION()
	void OnRep_Village(const TArray<FVillageUpgradesEntry>& Previous);

	/** Un village qui n'a rien acheté n'y figure pas. */
	UPROPERTY(ReplicatedUsing = OnRep_Village)
	TArray<FVillageUpgradesEntry> VillageUpgrades;

	/** Un village chargé sur cette machine : ses acteurs locaux. */
	struct FLocalVillage
	{
		TMap<EShopKind, TWeakObjectPtr<AActor>> Shops;
		TArray<TWeakObjectPtr<AActor>> Houses;
		TWeakObjectPtr<AActor> Fountain;
		TWeakObjectPtr<AActor> Decor;
	};
	TMap<FIntPoint, FLocalVillage> LocalVillages;
	FTimerHandle VillageStreamTimer;
	/** La graine sous laquelle les villages chargés ont été placés : si elle change, tout se replace. */
	int32 StreamedSeed = 0;

	/** Les clients posent la graine des villages et des routes dès qu'elle arrive. */
	UFUNCTION()
	void OnRep_WorldSeed();

	UPROPERTY(ReplicatedUsing = OnRep_WorldSeed)
	int32 WorldSeed = 0;

	/** 0 : météo naturelle ; sinon ErewhonWeather::EKind + 1. Non sauvegardé : une triche de session. */
	UPROPERTY(Replicated)
	uint8 ForcedWeather = 0;

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
