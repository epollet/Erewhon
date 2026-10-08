#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ErewhonPlayerState.generated.h"

class UInventoryComponent;

DECLARE_MULTICAST_DELEGATE(FOnCoinsChanged);

/** Identité persistante, pièces et inventaire d'un joueur. */
UCLASS()
class EREWHON_API AErewhonPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AErewhonPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UInventoryComponent* GetInventory() const { return Inventory; }

	/** Clé stable du joueur entre les sessions — vide tant que le client ne s'est pas présenté. */
	const FString& GetPlayerKey() const { return PlayerKey; }
	bool IsRegistered() const { return !PlayerKey.IsEmpty(); }

	int32 GetCoins() const { return Coins; }

	// Serveur
	void SetPlayerKey(const FString& InKey) { PlayerKey = InKey; }
	void SetCoins(int32 InCoins);
	bool SpendCoins(int32 Amount);
	void AddCoins(int32 Amount);

	FOnCoinsChanged OnCoinsChanged;

private:
	UFUNCTION()
	void OnRep_Coins();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInventoryComponent> Inventory;

	UPROPERTY(Replicated)
	FString PlayerKey;

	UPROPERTY(ReplicatedUsing = OnRep_Coins)
	int32 Coins = 0;
};
