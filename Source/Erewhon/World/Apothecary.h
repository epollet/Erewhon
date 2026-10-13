#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "Inventory/InventoryComponent.h"
#include "Apothecary.generated.h"

class AErewhonGameState;
class ADoor;
class ASpawnCabin;
class UAudioComponent;
class UItemDatabase;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

/** Bâtiments qu'on peut construire sur un secteur à soi (bits de FSectorOwnerEntry::Buildings). */
namespace ESectorBuilding
{
	constexpr uint8 Apothecary = 1 << 0;
}

/**
 * L'apothicairerie : une maisonnette au centre d'une prairie achetée, avec une cheminée,
 * un mortier (plantes → écrasés) et un petit chaudron (écrasés → bases, puis potions).
 * Toutes les longueurs sont en cm, dans le repère de la maison (porte côté −X).
 */
namespace ErewhonApothecary
{
	/** Clairière de terre autour de la maison. */
	constexpr float ClearingRadius = 1000.f;

	/** Durées : un écrasé au mortier, une préparation au chaudron, un effet de potion (s). */
	constexpr float CrushSeconds = 1.f;
	constexpr float BrewSeconds = 3.f;
	constexpr float PotionSeconds = 120.f;

	/** Une brassée de petit bois fait brûler la cheminée cinq minutes, une demi-heure au plus. */
	constexpr double FireSecondsPerWood = 300.0;
	constexpr double MaxFireSeconds = 1800.0;

	/** Distance au centre de la maison pour se servir du mortier et du chaudron. */
	constexpr float UseReach = 650.f;

	/** Le mortier sur la table, le foyer dans la cheminée et le chaudron posé dessus (repère de la maison, plancher compris). */
	EREWHON_API FVector MortarLocal();
	EREWHON_API FVector CauldronLocal();
	EREWHON_API FVector HearthLocal();

	/**
	 * Les étagères à potions, contre le mur de droite en entrant (+Y) : 3 étagères de 4 étages, une pile de 10 par étage.
	 * Les piles sont rangées étagère par étagère, de l'étage du bas (0) à celui du haut.
	 */
	constexpr int32 ShelfCount = 3;
	constexpr int32 ShelfRows = 4;
	constexpr int32 ShelfSlots = ShelfCount * ShelfRows;
	constexpr int32 PotionsPerSlot = 10;

	/** Ce qui se range sur les étagères : potions, bases et écrasés. */
	EREWHON_API bool IsShelvable(const struct FItemDef& Def);

	/** Repère d'une étagère dans la maison (façade vers −X, largeur sur Y, pied à Z = 0) et hauteur d'un rayon. */
	EREWHON_API FTransform ShelfLocal(int32 Shelf);
	EREWHON_API float ShelfRowZ(int32 Row);
	/** L'étagère d'une pile, et ses piles (première, après la dernière). */
	inline int32 ShelfOfSlot(int32 Slot) { return Slot / ShelfRows; }
	inline int32 FirstSlotOf(int32 Shelf) { return Shelf * ShelfRows; }

	/** Le côté (index dans ErewhonSector::Sides) vers lequel regarde la porte : la route voisine, sinon le village. */
	EREWHON_API int32 DoorSide(const FIntPoint& Sector);

	/** Repère de la maison : au centre du secteur, sur le point haut du terrain, porte tournée vers DoorSide. */
	EREWHON_API FTransform HouseTransform(const FIntPoint& Sector, int32 WorldSeed);

	EREWHON_API bool HasHouse(const AErewhonGameState* State, const FIntPoint& Sector);

	/** Dans une apothicairerie (sur son plancher), pour les bruits de pas. */
	EREWHON_API bool IsInside(const UObject* WorldContext, const FVector& World);

	/** Une préparation : ce qu'elle donne, ce qu'elle consomme, au mortier ou au chaudron. */
	struct FRecipe
	{
		FName Output;
		TArray<TPair<FName, int32>> Inputs;
		bool bCauldron = false;
		float Seconds() const { return bCauldron ? BrewSeconds : CrushSeconds; }
	};

	/** Toutes les préparations, dans l'ordre d'affichage (le mortier : une par plante ; le chaudron : la base puis les potions). */
	EREWHON_API TArray<FRecipe> Recipes(const UItemDatabase& Items, bool bCauldron);
	EREWHON_API TOptional<FRecipe> FindRecipe(const UItemDatabase& Items, FName Output);
}

/**
 * La maison de l'apothicaire, posée par le serveur quand le propriétaire l'achète.
 * Répliquée pour l'état du feu ; chaque machine construit localement la maison (ASpawnCabin),
 * les arbres alentour, la flambée et les postes de travail.
 */
UCLASS()
class EREWHON_API AApothecary : public AActor
{
	GENERATED_BODY()

public:
	AApothecary();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const FIntPoint& GetSector() const { return Sector; }
	bool IsFireLit() const;
	/** Secondes de feu restantes (0 s'il est éteint). */
	double FireRemaining() const;
	double GetFireUntil() const { return FireUntil; }

	/** Les potions rangées sur les étagères (ShelfSlots piles, rayon par rayon). */
	const TArray<FInventorySlot>& GetShelves() const { return Shelves; }
	FSimpleMulticastDelegate OnShelvesChanged;
	/** Le feu a été nourri (ou s'est rallumé). */
	FSimpleMulticastDelegate OnFireChanged;

	// ── Serveur ──

	/** À régler avant FinishSpawning. */
	void Setup(const FIntPoint& InSector, double InFireUntil, const TArray<FInventorySlot>& InShelves);
	TArray<FInventorySlot>& EditShelves() { return Shelves; }
	void CommitShelves();
	/** Une brassée de plus dans le feu. */
	void AddFire(double Seconds);

private:
	void BuildLocal();
	void UpdateFire(float DeltaSeconds);
	/** Les flacons posés sur les rayons, un par potion rangée. */
	void BuildShelfPotions();

	UFUNCTION()
	void OnRep_Shelves();

	UPROPERTY(ReplicatedUsing = OnRep_Shelves)
	TArray<FInventorySlot> Shelves;

	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ShelfBottles;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Replicated)
	FIntPoint Sector = FIntPoint::ZeroValue;

	/** Horloge du monde jusqu'à laquelle le feu brûle. */
	UPROPERTY(ReplicatedUsing = OnRep_Fire)
	double FireUntil = 0.0;

	UFUNCTION()
	void OnRep_Fire() { OnFireChanged.Broadcast(); }

	UPROPERTY(Transient) TObjectPtr<ASpawnCabin> House;
	UPROPERTY(Transient) TObjectPtr<ADoor> Door;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Stations;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> FireLight;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> FireSound;
	/** Les flammes peintes, chacune sur un pivot au ras des bûches (on les étire en hauteur). */
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> FlamePivots;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FlameGlows;

	bool bBuilt = false;
	float FireLevel = 0.f;   // 0 éteint → 1 flambée, adouci
	float FireTime = 0.f;
};

/** Ce qu'on vise dans l'apothicairerie. */
UENUM()
enum class EApothecaryStation : uint8
{
	Mortar,
	Cauldron,
	Hearth,
	Shelf,
};

/** Un poste de l'apothicairerie (acteur local) : on le vise et on l'active (E). */
UCLASS()
class EREWHON_API AApothecaryStation : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	AApothecaryStation();

	virtual void BeginPlay() override;
	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	void Setup(AApothecary* InHouse, EApothecaryStation InKind, int32 InShelf = 0);
	AApothecary* GetHouse() const { return House.Get(); }

private:
	/** Le joueur local est-il le propriétaire de la maison ? */
	bool IsLocalOwner() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UBoxComponent> Bounds;

	TWeakObjectPtr<AApothecary> House;
	EApothecaryStation Kind = EApothecaryStation::Mortar;
	/** Pour une étagère : laquelle (0 à ShelfCount − 1). */
	int32 ShelfIndex = 0;
};
