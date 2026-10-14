#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "World/SectorTypes.h"
#include "Inventory/InventoryComponent.h"
#include "World/Storage.h"
#include "ErewhonGameMode.generated.h"

class AErewhonPlayerController;
class AErewhonPlayerState;
class UErewhonWorldSave;

/**
 * Règles serveur : persistance du monde, récolte, commerce.
 * N'existe que sur le serveur — les clients passent par les RPC du PlayerController,
 * qui délèguent ici. Chaque Try* renvoie un message d'erreur, vide en cas de succès.
 */
UCLASS()
class EREWHON_API AErewhonGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AErewhonGameMode();

	virtual void InitGameState() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	void RegisterPlayer(AErewhonPlayerController* Controller, const FString& RequestedKey, const FString& DisplayName);

	FText TryHarvest(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 PlantIndex);
	FText TryBuySector(AErewhonPlayerController* Controller, const FIntPoint& Sector);
	FText TrySellSector(AErewhonPlayerController* Controller, const FIntPoint& Sector);
	FText TryBuyRoadLink(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 Side);

	// Apothicairerie
	FText TryBuyApothecary(AErewhonPlayerController* Controller, const FIntPoint& Sector);
	FText TryFeedFire(AErewhonPlayerController* Controller, class AApothecary* House);
	/** Au mortier ou au chaudron (meubles posés) : Count exemplaires, un à la fois. */
	FText TryStartCraft(AErewhonPlayerController* Controller, class AStorageActor* Station, FName Output, int32 Count);
	FText TryBuyStoreroom(AErewhonPlayerController* Controller, const FIntPoint& Sector);

	// L'eau
	/** Le chaudron puise dans les tonneaux d'eau tout proches, jusqu'à 10 L. */
	FText TryFillCauldron(AErewhonPlayerController* Controller, class AStorageActor* Cauldron);
	/** Vide un tonneau d'eau : il redevient un tonneau ordinaire. */
	FText TryEmptyWater(AErewhonPlayerController* Controller, class AStorageActor* Barrel);

	// Sac à dos
	FText TryDropBackpack(AErewhonPlayerController* Controller);
	FText TryPickUpBackpack(AErewhonPlayerController* Controller, class ABackpack* Backpack);
	/** Déplace un objet entre l'inventaire et le sac (To = INDEX_NONE : là où il trouve de la place). */
	FText TryMoveItem(AErewhonPlayerController* Controller, bool bFromBag, int32 From, bool bToBag, int32 To);
	FText TryStashAll(AErewhonPlayerController* Controller);
	FText TryDiscard(AErewhonPlayerController* Controller, bool bFromBag, int32 SlotIndex, class AStorageActor* Storage = nullptr);

	// Charrette et conteneurs
	FText TryPickUpStorage(AErewhonPlayerController* Controller, class AStorageActor* Storage);
	/** Pose ce qu'on tient : au sol (Type 0), sur une cagette (1), dans une charrette (2). */
	FText TryPlace(AErewhonPlayerController* Controller, uint8 Type, const FVector& Location, float Yaw, AActor* Target, int32 Slot);
	FText TryToggleDrive(AErewhonPlayerController* Controller, class ACart* Cart);
	FText TryMoveStorageItem(AErewhonPlayerController* Controller, class AStorageActor* Storage, bool bFromStorage, int32 From, bool bToStorage, int32 To);
	FText TryStashInStorage(AErewhonPlayerController* Controller, class AStorageActor* Storage);

	// Triche (hôte, ou build de développement)
	FText TryCheatSetCoins(AErewhonPlayerController* Controller, int32 Coins);
	FText TryCheatAdvanceTime(AErewhonPlayerController* Controller, float Seconds);

	// Village
	FText TryBuyVillageUpgrade(AErewhonPlayerController* Controller, uint8 Upgrade);
	FText TryBuyPavedLink(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 Side);
	FText TryBuyItem(AErewhonPlayerController* Controller, FName ItemId);
	FText TrySellItem(AErewhonPlayerController* Controller, FName ItemId, int32 Count);

	void SaveWorld();

private:
	bool IsNearMerchant(const AController* Controller, EShopKind Kind) const;
	void CapturePlayer(const AErewhonPlayerState* PlayerState);
	/** Les portes des cabanes, dont celle du tailleur une fois construit. */
	void SpawnDoors();
	void SpawnSavedObjects();
	void CaptureObjects();
	/** Un joueur qui part lâche ce qu'il porte et la charrette qu'il conduit. */
	void ReleaseHeld(APawn* Pawn);
	TSet<EShopKind> DoorsSpawned;

	class AApothecary* SpawnApothecary(const FIntPoint& Sector, double FireUntil);
	/**
	 * Le mobilier d'une apothicairerie d'avant le mobilier à acheter (ou des tests) : une table et son mortier,
	 * le chaudron à la crémaillère, trois étagères garnies de Shelves (douze piles, quatre par étagère).
	 */
	void SpawnFurnishing(const FIntPoint& Sector, const FString& OwnerKey, const FString& OwnerName, const TArray<FInventorySlot>& Shelves);
	class AStorageActor* SpawnStorage(EStorageKind Kind, const FTransform& At, const FString& OwnerKey, const FString& OwnerName);
	/** Un exemplaire de la préparation en cours est prêt : on l'échange contre ses ingrédients. */
	void StepCraft(AErewhonPlayerController* Controller);
	/** Le joueur est-il chez lui, assez près de sa maison (pour le feu) ? */
	FText CheckAtHouse(AErewhonPlayerController* Controller, const class AApothecary* House) const;
	/** Le joueur est-il à côté de son mortier ou de son chaudron ? */
	FText CheckAtStation(AErewhonPlayerController* Controller, const class AStorageActor* Station) const;

	UPROPERTY()
	TMap<FIntPoint, TObjectPtr<class AApothecary>> Apothecaries;

	/** La fontaine remplit les tonneaux posés sous son bec, à 1 C le litre payé par leur propriétaire. */
	void FillAtFountain();
	FTimerHandle FountainTimer;
	TSet<TWeakObjectPtr<class AStorageActor>> BrokeAtFountain;

	UPROPERTY()
	TObjectPtr<UErewhonWorldSave> WorldSave;

	FTimerHandle AutosaveTimer;
	FString SaveSlot;
};
