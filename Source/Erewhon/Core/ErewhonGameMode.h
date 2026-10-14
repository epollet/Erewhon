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

	/** Sans bNow, la cueillette prend son temps (une barre) et se fait au bout. */
	FText TryHarvest(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 PlantIndex, bool bNow = false);
	FText TryBuySector(AErewhonPlayerController* Controller, const FIntPoint& Sector);
	FText TrySellSector(AErewhonPlayerController* Controller, const FIntPoint& Sector);
	FText TryBuyRoadLink(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 Side);

	// Apothicairerie
	FText TryBuyApothecary(AErewhonPlayerController* Controller, const FIntPoint& Sector);
	FText TryFeedFire(AErewhonPlayerController* Controller, class AApothecary* House);
	/** Au mortier ou au chaudron (meubles posés) : Count exemplaires, un à la fois. */
	FText TryStartCraft(AErewhonPlayerController* Controller, class AStorageActor* Station, FName Output, int32 Count);
	FText TryBuyStoreroom(AErewhonPlayerController* Controller, const FIntPoint& Sector);
	/** « Débloquer » (menu) : on descend de selle, on lâche la charrette, et on se retrouve devant la mairie. */
	FText Unstuck(AErewhonPlayerController* Controller);
	/** Une place libre devant l'atelier, pour y livrer une charrette ou un outil attelé. */
	TOptional<FVector> FindWorkshopSpot() const;
	void DeliverCart(class AErewhonPlayerState* PlayerState, FName ItemId, const FVector& Spot);

	// Les liquides (2 L/s d'un récipient à l'autre)
	/**
	 * Op : 2 remplir des fioles au récipient (Count : combien) ; 5 puiser au pichet qu'on tient en main, 6 en verser :
	 * un litre par seconde, jusqu'à ce que le pichet (2 L) ou le récipient soit plein ou vide.
	 */
	FText TryStartPour(AErewhonPlayerController* Controller, class AStorageActor* Container, uint8 Op, FName Item, int32 Count);
	/** Vide un tonneau ou le chaudron (le liquide, et les ingrédients du chaudron) : perdu. */
	FText TryEmptyWater(AErewhonPlayerController* Controller, class AStorageActor* Container);
	FText TryAddIngredient(AErewhonPlayerController* Controller, class AStorageActor* Cauldron, FName ItemId, int32 Count);
	FText TryAdjustStand(AErewhonPlayerController* Controller, class AStorageActor* Stand, float Level);
	FText TryChooseClass(AErewhonPlayerController* Controller, uint8 NewClass);

	// Les champs du fermier (Core/ErewhonGameModeFarm.cpp)
	/** Which : 0-3 un champ (ErewhonFarm::EQuarter), 4 la fusion des quatre. */
	FText TryBuyField(AErewhonPlayerController* Controller, const FIntPoint& Sector, uint8 Which);
	/** Selon ce qu'on tient : bêche, sac de graines, cerpe ou mains nues. */
	FText TryWorkField(AErewhonPlayerController* Controller, const FIntPoint& Sector, int32 Cell, uint8 Corner = 0, bool bNow = false);
	/** Un coup de faux : deux secondes, puis tout ce qui est à portée est récolté. */
	FText TryScythe(AErewhonPlayerController* Controller);

	// Le bhurak (Core/ErewhonGameModeBhurak.cpp)
	/** Monter son bhurak sellé ; nul : descendre. */
	FText TryRideBhurak(AErewhonPlayerController* Controller, class ABhurak* Bhurak);
	/** Atteler (en selle près d'une charrette, ou en amenant la charrette) ou dételer. */
	FText TryHitchBhurak(AErewhonPlayerController* Controller, class ABhurak* Aimed);
	/** Slot 0-2 : sacoche ou selle (Item nul : retirer) ; 3 : le harnais ; 4 : reprendre le bhurak. */
	FText TryEquipBhurak(AErewhonPlayerController* Controller, class ABhurak* Bhurak, uint8 Slot, FName Item);
	/** Le levier d'un outil tracté (cultivateur, semoir). */
	FText TryToggleImplement(AErewhonPlayerController* Controller, class ACart* Implement);

	// Sac à dos
	FText TryDropBackpack(AErewhonPlayerController* Controller);
	FText TryPickUpBackpack(AErewhonPlayerController* Controller, class ABackpack* Backpack);
	/** Déplace un objet entre l'inventaire et le sac (To = INDEX_NONE : là où il trouve de la place). */
	FText TryMoveItem(AErewhonPlayerController* Controller, bool bFromBag, int32 From, bool bToBag, int32 To);
	FText TryStashAll(AErewhonPlayerController* Controller);
	FText TryDiscard(AErewhonPlayerController* Controller, bool bFromBag, int32 SlotIndex, class AStorageActor* Storage = nullptr);
	/** Ramasser des objets posés à terre (au propriétaire du terrain, ou à tous sur un lieu public). */
	FText TryPickUpDropped(AErewhonPlayerController* Controller, class ADroppedItem* Dropped);
	/** Poser des objets à terre devant soi : ils y restent trois minutes. */
	void DropOnGround(const APawn* Pawn, FName ItemId, int32 Count);

	// Charrette et conteneurs
	FText TryPickUpStorage(AErewhonPlayerController* Controller, class AStorageActor* Storage);
	/** Pose ce qu'on tient : au sol (Type 0), sur une cagette (1), dans une charrette (2). */
	FText TryPlace(AErewhonPlayerController* Controller, uint8 Type, const FVector& Location, float Yaw, AActor* Target, int32 Slot);
	FText TryToggleDrive(AErewhonPlayerController* Controller, class ACart* Cart);
	FText TryMoveStorageItem(AErewhonPlayerController* Controller, class AStorageActor* Storage, bool bFromStorage, int32 From, bool bToStorage, int32 To);
	FText TryStashInStorage(AErewhonPlayerController* Controller, class AStorageActor* Storage);
	/** Le petit bois en vrac de la charrette. */
	FText TryMoveCartItem(AErewhonPlayerController* Controller, class ACart* Cart, bool bFromCart, int32 From, bool bToCart, int32 To);
	FText TryStashInCart(AErewhonPlayerController* Controller, class ACart* Cart);
	/**
	 * Une partie d'une pile, d'une case à une autre : de ses poches (bFromOther faux) ou de l'autre côté (le sac,
	 * ou Storage, ou Cart), vers une case vide ou de la même chose.
	 */
	FText TrySplitMove(AErewhonPlayerController* Controller, bool bFromOther, int32 From, bool bToOther, int32 To, int32 Count,
		class AStorageActor* Storage, class ACart* Cart);

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
	class ABhurak* SpawnBhurak(const FTransform& At, const FString& OwnerKey, const FString& OwnerName, uint8 Coat = 0);
	/** Un exemplaire de la préparation en cours est prêt : on l'échange contre ses ingrédients. */
	void StepCraft(AErewhonPlayerController* Controller);
	/** Le joueur est-il chez lui, assez près de sa maison (pour le feu) ? */
	FText CheckAtHouse(AErewhonPlayerController* Controller, const class AApothecary* House) const;
	/** Un transvasement en cours, par joueur. */
	struct FPourOp { uint8 Op = 0; TWeakObjectPtr<class AStorageActor> Container; FName Item; };
	TMap<TWeakObjectPtr<AErewhonPlayerController>, FPourOp> PourOps;
	void StepPour(AErewhonPlayerController* Controller);
	/** Seul un apothicaire se sert des outils de l'apothicairerie. */
	FText CheckApothecary(AErewhonPlayerController* Controller) const;
	FText CheckFarmer(AErewhonPlayerController* Controller) const;
	FText FinishScythe(AErewhonPlayerController* Controller);
	void ReportResultTo(AErewhonPlayerController* Controller, const FText& Error);
	/** Le coup de bêche en cours, par joueur : les cases qu'il retournera au bout d'une seconde. */
	struct FPendingDig { FIntPoint Sector = FIntPoint::ZeroValue; TArray<int32> Cells; };
	TMap<TWeakObjectPtr<AErewhonPlayerController>, FPendingDig> PendingDigs;
	void FinishDig(AErewhonPlayerController* Controller);
	/** Un geste qui prend du temps (cueillir à la main, à la cerpe) : sa barre, puis Finish au bout. Faux si un autre est en cours. */
	bool StartTimedGesture(AErewhonPlayerController* Controller, float Seconds, uint8 Pour, TFunction<FText(AErewhonPlayerController*)> Finish);
	/** La faux en cours, par joueur (heure de fin). */
	TMap<TWeakObjectPtr<AErewhonPlayerController>, double> ScytheUntil;

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
