#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ErewhonPlayerState.generated.h"

class UBackpackComponent;
class UInventoryComponent;

DECLARE_MULTICAST_DELEGATE(FOnCoinsChanged);

/** La classe qu'on se choisit à la mairie (un seul choix). */
UENUM()
enum class EPlayerClass : uint8
{
	None,         // aucune classe particulière
	Apothecary,   // seul un apothicaire se sert des outils de l'apothicairerie
	Farmer,       // seul un fermier achète des champs et la faux
	Forester,     // seul un forestier manie la hache : il abat les arbres de ses secteurs et en tire des bûches
};

/** Identité persistante, pièces et inventaire d'un joueur. */
UCLASS()
class EREWHON_API AErewhonPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AErewhonPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UInventoryComponent* GetInventory() const { return Inventory; }
	UBackpackComponent* GetBackpack() const { return Backpack; }

	/** Clé stable du joueur entre les sessions — vide tant que le client ne s'est pas présenté. */
	const FString& GetPlayerKey() const { return PlayerKey; }
	bool IsRegistered() const { return !PlayerKey.IsEmpty(); }

	int32 GetCoins() const { return Coins; }
	EPlayerClass GetPlayerClass() const { return PlayerClass; }
	bool IsApothecary() const { return PlayerClass == EPlayerClass::Apothecary; }
	bool IsFarmer() const { return PlayerClass == EPlayerClass::Farmer; }
	bool IsForester() const { return PlayerClass == EPlayerClass::Forester; }

	// Serveur
	void SetPlayerKey(const FString& InKey) { PlayerKey = InKey; }
	void SetCoins(int32 InCoins);
	bool SpendCoins(int32 Amount);
	void AddCoins(int32 Amount);
	void SetPlayerClass(EPlayerClass InClass) { PlayerClass = InClass; OnCoinsChanged.Broadcast(); }

	FOnCoinsChanged OnCoinsChanged;

private:
	UFUNCTION()
	void OnRep_Coins();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInventoryComponent> Inventory;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBackpackComponent> Backpack;

	UPROPERTY(Replicated)
	FString PlayerKey;

	UPROPERTY(ReplicatedUsing = OnRep_Coins)
	int32 Coins = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Coins)
	EPlayerClass PlayerClass = EPlayerClass::None;
};
