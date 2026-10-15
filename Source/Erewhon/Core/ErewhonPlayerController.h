#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "World/SectorTypes.h"
#include "ErewhonPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class SWidget;

/** Actions d'entrée créées par code (pas d'assets à maintenir). */
USTRUCT()
struct FErewhonInputActions
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	UPROPERTY() TObjectPtr<UInputAction> Move;
	UPROPERTY() TObjectPtr<UInputAction> Look;
	UPROPERTY() TObjectPtr<UInputAction> Jump;
	UPROPERTY() TObjectPtr<UInputAction> Run;
	UPROPERTY() TObjectPtr<UInputAction> Primary;
	UPROPERTY() TObjectPtr<UInputAction> Secondary;   // clic droit
	UPROPERTY() TObjectPtr<UInputAction> Inventory;
	UPROPERTY() TObjectPtr<UInputAction> CloseMenu;
	UPROPERTY() TObjectPtr<UInputAction> Use;
	UPROPERTY() TObjectPtr<UInputAction> Crouch;
	UPROPERTY() TObjectPtr<UInputAction> Bag;     // F : poser / ouvrir le sac à dos
	UPROPERTY() TObjectPtr<UInputAction> Stash;   // G : tout ranger dans le sac
	UPROPERTY() TObjectPtr<UInputAction> View;    // V : vue à la 1re / 3e personne
	UPROPERTY() TObjectPtr<UInputAction> Hotbar1; // 1 / & : premier emplacement de la barre
	UPROPERTY() TObjectPtr<UInputAction> Hotbar2; // 2 / é
	UPROPERTY() TObjectPtr<UInputAction> Hotbar3; // 3 / "
	UPROPERTY() TObjectPtr<UInputAction> HotbarScroll;   // molette
};

/** Une préparation en cours à l'apothicairerie : un exemplaire à la fois, jusqu'à épuisement. */
USTRUCT()
struct FErewhonCraftJob
{
	GENERATED_BODY()

	UPROPERTY() FName Output;
	/** Exemplaires restant à préparer, celui en cours compris. */
	UPROPERTY() int32 Remaining = 0;
	/** Horloge du monde à laquelle l'exemplaire en cours sera prêt. */
	UPROPERTY() double StepEnd = 0.0;
	UPROPERTY() float StepSeconds = 1.f;
	/** Au pichet : 1 on puise, 2 on verse ; 3 un coup de bêche ; 4 on cueille à la main, 5 à la cerpe (0 : une préparation). */
	UPROPERTY() uint8 Pour = 0;

	bool IsActive() const { return Remaining > 0 && !Output.IsNone(); }
};

struct FErewhonToast
{
	FText Text;
	double ExpireTime = 0.0;
};

UCLASS()
class EREWHON_API AErewhonPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

	/** Actions d'entrée, créées à la première demande (le pawn peut les réclamer avant BeginPlay). */
	const FErewhonInputActions& GetInputActions();

	// ── Menus ──

	/** Vrai aussi pendant l'image où un menu est en train de s'ouvrir. */
	bool IsMenuOpen() const { return OpenMenu.IsValid() || PendingMenu.IsValid(); }
	void ToggleInventory();
	/** Inventaire, avec à côté le sac à dos (porté, ou posé et visé : Bag). */
	void OpenInventory(class ABackpack* Bag);
	/** Inventaire, avec à côté le contenu d'une cagette ou d'un tonneau. */
	void OpenStorage(class AStorageActor* Storage);
	/** Inventaire, avec à côté le petit bois en vrac de la charrette. */
	void OpenCart(class ACart* Cart);
	void OpenStore(EShopKind Kind);
	/** Son mortier ou son chaudron (meubles posés). */
	void OpenApothecary(class AStorageActor* Station, uint8 Craft);
	/** Le chaudron : eau, feu, hauteur, température, ingrédients, breuvage. */
	void OpenCauldron(class AStorageActor* Cauldron);
	/** Le livre des breuvages, page par page. */
	void OpenBook();
	/** Le harnachement de son bhurak. */
	void OpenBhurak(class ABhurak* Bhurak);

	/** Panneau de triche (Ctrl+Alt+ù puis Ctrl+Maj+*). */
	void OpenCheats();
	void CloseMenu();

	/** Échap : ferme le menu ouvert, sinon ouvre le menu de jeu. */
	void HandleEscape();

	// ── Apothicairerie ──

	const FErewhonCraftJob& GetCraftJob() const { return CraftJob; }
	/** La préparation a changé (début, exemplaire prêt, fin). */
	FSimpleMulticastDelegate OnCraftChanged;

	/** Serveur : la préparation en cours, et la maison où elle se fait. */
	void SetCraftJob(const FErewhonCraftJob& Job, class AStorageActor* Station);
	class AStorageActor* GetCraftStation() const { return CraftStation.Get(); }
	FTimerHandle CraftTimer;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── Messages ──

	void ShowToast(const FText& Text);
	const TArray<FErewhonToast>& GetToasts() const { return Toasts; }

	// ── Requêtes au serveur ──

	UFUNCTION(Server, Reliable)
	void ServerHarvest(FIntPoint Sector, int32 PlantIndex);

	UFUNCTION(Server, Reliable)
	void ServerUseDoor(class ADoor* Door);

	UFUNCTION(Server, Reliable)
	void ServerBuySector(FIntPoint Sector);

	UFUNCTION(Server, Reliable)
	void ServerSellSector(FIntPoint Sector);

	UFUNCTION(Server, Reliable)
	void ServerBuyRoadLink(FIntPoint Sector, uint8 Side);

	UFUNCTION(Server, Reliable)
	void ServerBuyItem(FName ItemId);

	UFUNCTION(Server, Reliable)
	void ServerSellItem(FName ItemId, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerDropBackpack();

	UFUNCTION(Server, Reliable)
	void ServerPickUpBackpack(class ABackpack* Backpack);

	/** Déplace un objet entre l'inventaire et le sac ; To = INDEX_NONE : là où il y a de la place. */
	UFUNCTION(Server, Reliable)
	void ServerMoveItem(bool bFromBag, int32 From, bool bToBag, int32 To);

	/** Une partie d'une pile, posée dans une case (voir AErewhonGameMode::TrySplitMove). */
	UFUNCTION(Server, Reliable)
	void ServerSplitMove(bool bFromOther, int32 From, bool bToOther, int32 To, int32 Count, class AStorageActor* Storage, class ACart* Cart);

	UFUNCTION(Server, Reliable)
	void ServerStashAll();

	/** Jette un exemplaire de l'objet de ce slot (de ses poches, du sac posé, ou d'un conteneur). */
	UFUNCTION(Server, Reliable)
	void ServerDiscard(bool bFromBag, int32 SlotIndex, class AStorageActor* Storage);

	UFUNCTION(Server, Reliable)
	void ServerPickUpStorage(class AStorageActor* Storage);

	UFUNCTION(Server, Reliable)
	void ServerPlace(uint8 Type, FVector Location, float Yaw, AActor* Target, int32 Slot);

	/** Prendre la charrette visée (devant ses brancards), ou lâcher celle qu'on conduit (Cart nul). */
	UFUNCTION(Server, Reliable)
	void ServerToggleDrive(class ACart* Cart);

	UFUNCTION(Server, Reliable)
	void ServerMoveStorageItem(class AStorageActor* Storage, bool bFromStorage, int32 From, bool bToStorage, int32 To);

	UFUNCTION(Server, Reliable)
	void ServerStashInStorage(class AStorageActor* Storage);

	UFUNCTION(Server, Reliable)
	void ServerMoveCartItem(class ACart* Cart, bool bFromCart, int32 From, bool bToCart, int32 To);

	UFUNCTION(Server, Reliable)
	void ServerStashInCart(class ACart* Cart);

	UFUNCTION(Server, Reliable)
	void ServerBuyVillageUpgrade(uint8 Upgrade);

	UFUNCTION(Server, Reliable)
	void ServerCheatSetCoins(int32 Coins);

	UFUNCTION(Server, Reliable)
	void ServerCheatAdvanceTime(float Seconds);

	UFUNCTION(Server, Reliable)
	void ServerBuyPavedLink(FIntPoint Sector, uint8 Side);

	UFUNCTION(Server, Reliable)
	void ServerBuyApothecary(FIntPoint Sector);

	/** Une brassée de petit bois dans la cheminée. */
	UFUNCTION(Server, Reliable)
	void ServerFeedFire(class AApothecary* House);

	/** Préparer Count exemplaires d'Output (un écrasé, une base, une potion), un à la fois. */
	UFUNCTION(Server, Reliable)
	void ServerStartCraft(class AStorageActor* Station, FName Output, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerStopCraft();

	/** Transvaser (voir AErewhonGameMode::TryStartPour). */
	UFUNCTION(Server, Reliable)
	void ServerStartPour(class AStorageActor* Container, uint8 Op, FName Item, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerAddIngredient(class AStorageActor* Cauldron, FName ItemId, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerAdjustStand(class AStorageActor* Stand, float Level);

	UFUNCTION(Server, Reliable)
	void ServerChooseClass(uint8 NewClass);

	/** Les champs du fermier (voir AErewhonGameMode::TryBuyField, TryWorkField, TryScythe). */
	UFUNCTION(Server, Reliable)
	void ServerBuyField(FIntPoint Sector, uint8 Which);

	UFUNCTION(Server, Reliable)
	void ServerWorkField(FIntPoint Sector, int32 Cell, uint8 Corner);

	UFUNCTION(Server, Reliable)
	void ServerScythe();

	/** Le bhurak (voir AErewhonGameMode::TryRideBhurak, TryHitchBhurak, TryEquipBhurak). */
	UFUNCTION(Server, Reliable)
	void ServerRideBhurak(class ABhurak* Bhurak);

	UFUNCTION(Server, Reliable)
	void ServerHitchBhurak(class ABhurak* Bhurak);

	UFUNCTION(Server, Reliable)
	void ServerEquipBhurak(class ABhurak* Bhurak, uint8 Slot, FName Item);

	UFUNCTION(Server, Reliable)
	void ServerToggleImplement(class ACart* Implement);

	/** Ramasser des objets posés à terre. */
	UFUNCTION(Server, Reliable)
	void ServerPickUpDropped(class ADroppedItem* Dropped);

	UFUNCTION(Server, Reliable)
	void ServerEmptyWater(class AStorageActor* Barrel);

	/** La remise de l'apothicairerie. */
	UFUNCTION(Server, Reliable)
	void ServerBuyStoreroom(FIntPoint Sector);

	/** Le bouton « Débloquer » du menu : devant la mairie. */
	UFUNCTION(Server, Reliable)
	void ServerUnstuck();

	/** Forestier : un coup de hache dans un arbre, une bûche détachée d'un tronc, des bûches reprises sur un tas. */
	UFUNCTION(Server, Reliable)
	void ServerChopTree(FIntPoint Sector, int32 TreeIndex);
	UFUNCTION(Server, Reliable)
	void ServerChopLog(class AFelledTree* Felled);
	UFUNCTION(Server, Reliable)
	void ServerTakeLogs(class AStorageActor* Pile);

	/** Un exemplaire est prêt : le bruit du pilon ou du chaudron. */
	UFUNCTION(Client, Unreliable)
	void ClientCraftStep(uint8 Craft);

	UFUNCTION(Client, Reliable)
	void ClientToast(const FText& Text);

private:
	UFUNCTION(Server, Reliable)
	void ServerRegisterPlayer(const FString& PlayerKey, const FString& DisplayName);

	void ReportResult(const FText& Error);
	void ShowMenu(const TSharedRef<SWidget>& Widget, bool bIsStore);
	FString LoadOrCreatePlayerKey() const;

	UPROPERTY()
	FErewhonInputActions Actions;

#if !UE_BUILD_SHIPPING
	void StartAutoShots();
	void NextAutoShot();
	TArray<float> ShotYaws;
	float ShotPitch = -6.f;
	FVector ShotLocation = FVector::ZeroVector;
	bool bShotHasLocation = false;
	FString ShotMenu;
	int32 ShotIndex = 0;
	FTimerHandle ShotTimer;
#endif

	TSharedPtr<SWidget> OpenMenu;
	TSharedPtr<SWidget> PendingMenu;
	bool bStoreOpen = false;
	EShopKind StoreKind = EShopKind::GeneralStore;
	TWeakObjectPtr<class ABackpack> OpenedBag;
	TWeakObjectPtr<class AStorageActor> OpenedStorage;
	TWeakObjectPtr<class ACart> OpenedCart;
	TWeakObjectPtr<AActor> OpenedStation;
	TWeakObjectPtr<class ABhurak> OpenedBhurak;
	TWeakPtr<SWidget> BhurakWidget;
	TWeakPtr<SWidget> ApothecaryWidget;

	UFUNCTION()
	void OnRep_CraftJob() { OnCraftChanged.Broadcast(); }

	UPROPERTY(ReplicatedUsing = OnRep_CraftJob)
	FErewhonCraftJob CraftJob;

	TWeakObjectPtr<class AStorageActor> CraftStation;

	/** Écoute des raccourcis de triche, avant les entrées du jeu et des menus. */
	TSharedPtr<class IInputProcessor> CheatKeys;
	TArray<FErewhonToast> Toasts;
};
