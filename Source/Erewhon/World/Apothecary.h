#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "Inventory/InventoryComponent.h"
#include "Apothecary.generated.h"

class AErewhonGameState;
class ADoor;
class ASpawnCabin;
class AStorageActor;
class UAudioComponent;
class UItemDatabase;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/** Bâtiments qu'on peut construire sur un secteur à soi (bits de FSectorOwnerEntry::Buildings). */
namespace ESectorBuilding
{
	constexpr uint8 Apothecary = 1 << 0;
	/** La remise, une petite pièce accolée à droite de l'apothicairerie. */
	constexpr uint8 Storeroom = 1 << 1;
}

/**
 * L'apothicairerie : une maisonnette au centre d'une prairie achetée, avec sa cheminée et sa crémaillère.
 * On la meuble soi-même (mobilier du General Store) : étagères, table, mortier, chaudron.
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

	/** Distance d'un meuble (mortier, chaudron) pour s'en servir. */
	constexpr float UseReach = 450.f;

	/**
	 * La cheminée modélisée (SM_fireplace), contre le mur du fond : sa largeur et sa profondeur (cm), et la rotation
	 * qui tourne sa façade vers la pièce. Sans le modèle, une cheminée en blocs de pierre la remplace.
	 */
	constexpr float FireplaceWidth = 300.f;
	constexpr float FireplaceDepth = 140.f;
	EREWHON_API bool HasFireplaceModel();

	/** Le foyer dans la cheminée, et le crochet de la crémaillère où pend le chaudron (repère de la maison). */
	EREWHON_API FVector HearthLocal();
	EREWHON_API FVector CauldronLocal();

	/** La remise : à droite en entrant (+Y), une porte à la place de la fenêtre, une pièce de 4 × 3 m. */
	constexpr float StoreroomHalfLength = 200.f;
	constexpr float StoreroomDepth = 300.f;

	/** Une étagère à potions : quatre étages, une pile de dix par étage. */
	constexpr int32 ShelfRows = 4;
	constexpr int32 PotionsPerSlot = 10;
	constexpr float ShelfWidth = 120.f;
	constexpr float ShelfDepth = 36.f;
	constexpr float ShelfHeight = 190.f;

	/** Ce qui se range sur les étagères : potions, bases, écrasés, teintures et pots-pourris. */
	EREWHON_API bool IsShelvable(const struct FItemDef& Def);

	/** Hauteur du dessus d'un étage de l'étagère (0 : en bas). */
	EREWHON_API float ShelfRowZ(int32 Row);

	/** Les planches d'une étagère (façade vers −X, largeur sur Y, pied à Z = 0). */
	EREWHON_API void BuildShelfFrame(AActor* Owner, USceneComponent* Parent);

	/** Les flacons et les bols d'une étagère, étage par étage, avec la lueur des potions ; refait Parts. */
	EREWHON_API void BuildShelfDisplay(AActor* Owner, USceneComponent* Parent, const TArray<FInventorySlot>& Slots,
		TArray<TObjectPtr<UPrimitiveComponent>>& Parts);

	/** Le côté (index dans ErewhonSector::Sides) vers lequel regarde la porte : la route voisine, sinon le village. */
	EREWHON_API int32 DoorSide(const FIntPoint& Sector);

	/** Repère de la maison : au centre du secteur, sur le point haut du terrain, porte tournée vers DoorSide. */
	EREWHON_API FTransform HouseTransform(const FIntPoint& Sector, int32 WorldSeed);

	EREWHON_API bool HasHouse(const AErewhonGameState* State, const FIntPoint& Sector);
	EREWHON_API bool HasStoreroom(const AErewhonGameState* State, const FIntPoint& Sector);

	/** Le crochet de la crémaillère de la maison de ce secteur (monde), s'il y en a une. */
	EREWHON_API TOptional<FTransform> HookTransform(const UObject* WorldContext, const FIntPoint& Sector);

	/** La maison d'un secteur (acteur répliqué), s'il y en a une. */
	EREWHON_API class AApothecary* FindHouse(const UObject* WorldContext, const FIntPoint& Sector);

	/** Ce chaudron pend-il à une crémaillère ? Renvoie la maison. */
	EREWHON_API class AApothecary* HouseOfHungCauldron(const AStorageActor* Cauldron);

	/** Dans une apothicairerie ou sa remise (sur le plancher), pour les bruits de pas. */
	EREWHON_API bool IsInside(const UObject* WorldContext, const FVector& World);

	/** Les postes où l'on prépare. */
	enum class ECraft : uint8 { Mortar, Cauldron, Workbench };
	constexpr float WorkbenchSeconds = 4.f;

	/** Une préparation : ce qu'elle donne, ce qu'elle consomme, et à quel poste. */
	struct FRecipe
	{
		FName Output;
		TArray<TPair<FName, int32>> Inputs;
		ECraft Craft = ECraft::Mortar;
		float Seconds() const { return Craft == ECraft::Cauldron ? BrewSeconds : Craft == ECraft::Workbench ? WorkbenchSeconds : CrushSeconds; }
	};

	/**
	 * Toutes les préparations d'un poste, dans l'ordre d'affichage : le mortier, une par plante ; le chaudron, la base
	 * puis les potions ; l'atelier, le pot-pourri, les teintures et les bouquets.
	 */
	EREWHON_API TArray<FRecipe> Recipes(const UItemDatabase& Items, ECraft Craft);
	EREWHON_API TOptional<FRecipe> FindRecipe(const UItemDatabase& Items, FName Output);
}

/**
 * La maison de l'apothicaire, posée par le serveur quand le propriétaire l'achète.
 * Répliquée pour l'état du feu ; chaque machine construit localement la maison (ASpawnCabin),
 * sa remise si elle est construite, les arbres alentour et la flambée.
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

	/** Le feu a été nourri (ou s'est rallumé). */
	FSimpleMulticastDelegate OnFireChanged;

	// ── Serveur ──

	/** À régler avant FinishSpawning. */
	void Setup(const FIntPoint& InSector, double InFireUntil);
	/** Une brassée de plus dans le feu. */
	void AddFire(double Seconds);

private:
	void BuildLocal();
	/** La maison (et sa remise) : refaite quand on achète la remise. */
	void BuildHouse();
	void OnSectorOwnerChanged(const FIntPoint& Changed);
	void UpdateFire(float DeltaSeconds);

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
	UPROPERTY(Transient) TObjectPtr<AActor> HearthStation;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> FireLight;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> FireSound;
	/** Les flammes peintes, chacune sur un pivot au ras des bûches (on les étire en hauteur). */
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> FlamePivots;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FlameGlows;

	FDelegateHandle OwnerHandle;
	bool bBuilt = false;
	bool bHouseHasStoreroom = false;
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
};

/** Le foyer de la cheminée (acteur local) : on le vise et on y met du petit bois (E). */
UCLASS()
class EREWHON_API AApothecaryStation : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	AApothecaryStation();

	virtual void BeginPlay() override;
	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	void Setup(AApothecary* InHouse) { House = InHouse; }
	AApothecary* GetHouse() const { return House.Get(); }

private:
	/** Le joueur local est-il le propriétaire de la maison ? */
	bool IsLocalOwner() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UBoxComponent> Bounds;

	TWeakObjectPtr<AApothecary> House;
};
