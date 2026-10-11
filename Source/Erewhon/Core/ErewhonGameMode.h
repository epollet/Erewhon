#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "World/SectorTypes.h"
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

	// Sac à dos
	FText TryDropBackpack(AErewhonPlayerController* Controller);
	FText TryPickUpBackpack(AErewhonPlayerController* Controller, class ABackpack* Backpack);
	/** Déplace un objet entre l'inventaire et le sac (To = INDEX_NONE : là où il trouve de la place). */
	FText TryMoveItem(AErewhonPlayerController* Controller, bool bFromBag, int32 From, bool bToBag, int32 To);
	FText TryStashAll(AErewhonPlayerController* Controller);
	FText TryBuyItem(AErewhonPlayerController* Controller, FName ItemId);
	FText TrySellItem(AErewhonPlayerController* Controller, FName ItemId, int32 Count);

	void SaveWorld();

private:
	bool IsNearMerchant(const AController* Controller, EShopKind Kind) const;
	void CapturePlayer(const AErewhonPlayerState* PlayerState);

	UPROPERTY()
	TObjectPtr<UErewhonWorldSave> WorldSave;

	FTimerHandle AutosaveTimer;
	FString SaveSlot;
};
