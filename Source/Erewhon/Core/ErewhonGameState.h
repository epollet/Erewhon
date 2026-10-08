#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
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

DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPlantHarvestChanged, const FIntPoint& /*Sector*/, int32 /*PlantIndex*/, bool /*bHarvested*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSectorOwnerChanged, const FIntPoint& /*Sector*/);

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

	// ── Serveur uniquement ──

	void InitializeWorld(int32 InSeed, double InWorldAge);
	void AddHarvest(const FIntPoint& Sector, int32 PlantIndex, double HarvestTime);
	void SetSectorOwner(const FIntPoint& Sector, const FString& OwnerKey, const FString& OwnerName);
	void ClearSectorOwner(const FIntPoint& Sector);
	const TArray<FHarvestEntry>& GetHarvestEntries() const { return Harvests.Items; }
	const TArray<FSectorOwnerEntry>& GetOwnerEntries() const { return SectorOwners.Items; }

	FOnPlantHarvestChanged OnPlantHarvestChanged;
	FOnSectorOwnerChanged OnSectorOwnerChanged;

	// Appelés par les fast arrays (et directement côté serveur).
	void NotifyHarvest(const FHarvestEntry& Entry, bool bHarvested);
	void NotifyOwner(const FSectorOwnerEntry& Entry, bool bOwned);

private:
	void PruneHarvests();
	void SpawnLocalWorldActors();

	UPROPERTY(Replicated)
	int32 WorldSeed = 0;

	/** Âge du monde au lancement du serveur ; l'horloge en découle. */
	UPROPERTY(Replicated)
	double WorldAgeAtStart = 0.0;

	UPROPERTY(Replicated)
	FHarvestArray Harvests;

	UPROPERTY(Replicated)
	FSectorOwnerArray SectorOwners;

	/** Caches locaux, tenus à jour par les notifications des fast arrays. */
	TMap<FIntPoint, TSet<int32>> HarvestedBySector;
	TMap<FIntPoint, FSectorOwnerEntry> OwnerBySector;

	FTimerHandle PruneTimer;
	bool bLocalActorsSpawned = false;
};
