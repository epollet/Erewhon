#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
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
	FText TryBuyItem(AErewhonPlayerController* Controller, FName ItemId);
	FText TrySellItem(AErewhonPlayerController* Controller, FName ItemId, int32 Count);

	void SaveWorld();

private:
	bool IsNearMerchant(const AController* Controller) const;
	void CapturePlayer(const AErewhonPlayerState* PlayerState);

	UPROPERTY()
	TObjectPtr<UErewhonWorldSave> WorldSave;

	FTimerHandle AutosaveTimer;
};
