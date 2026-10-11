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
	UPROPERTY() TObjectPtr<UInputAction> Inventory;
	UPROPERTY() TObjectPtr<UInputAction> CloseMenu;
	UPROPERTY() TObjectPtr<UInputAction> Use;
	UPROPERTY() TObjectPtr<UInputAction> Crouch;
	UPROPERTY() TObjectPtr<UInputAction> Bag;     // F : poser / ouvrir le sac à dos
	UPROPERTY() TObjectPtr<UInputAction> Stash;   // G : tout ranger dans le sac
	UPROPERTY() TObjectPtr<UInputAction> View;    // V : vue à la 1re / 3e personne
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
	void OpenStore(EShopKind Kind);
	void CloseMenu();

	/** Échap : ferme le menu ouvert, sinon ouvre le menu de jeu. */
	void HandleEscape();

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

	UFUNCTION(Server, Reliable)
	void ServerStashAll();

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
	TArray<FErewhonToast> Toasts;
};
