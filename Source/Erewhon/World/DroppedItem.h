#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/Interactable.h"
#include "DroppedItem.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Des objets posés à terre : sur un secteur à quelqu'un, seul son propriétaire les ramasse ; ailleurs,
 * tout le monde. Ils disparaissent au bout de trois minutes (le compte à rebours flotte au-dessus).
 */
UCLASS()
class EREWHON_API ADroppedItem : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	static constexpr double LifeSeconds = 180.0;

	ADroppedItem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(class AErewhonPlayerController* User) override;

	FName GetItemId() const { return ItemId; }
	/** Le nom, le nombre et le temps qui reste, pour l'étiquette du HUD. */
	FString GetLabel() const;
	double GetSecondsLeft() const;
	int32 GetCount() const { return Count; }
	/** Personne (lieu public) ou le joueur qui possède le terrain. */
	bool CanPickUp(const FString& PlayerKey) const { return LandOwnerKey.IsEmpty() || LandOwnerKey == PlayerKey; }

	// ── Serveur ──

	void Setup(FName InItemId, int32 InCount, const FString& InLandOwnerKey, const FString& InLandOwnerName, double InExpiresAt);
	/** Un même objet jeté au même endroit rejoint le tas (et le délai repart). */
	void AddCount(int32 More, double InExpiresAt);
	/** Retire des exemplaires ; le tas disparaît une fois vide. */
	void Take(int32 Taken);

private:
	UFUNCTION()
	void OnRep_Item();

	void ApplyItem();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(ReplicatedUsing = OnRep_Item)
	FName ItemId;

	UPROPERTY(ReplicatedUsing = OnRep_Item)
	int32 Count = 0;

	UPROPERTY(Replicated)
	FString LandOwnerKey;

	UPROPERTY(Replicated)
	FString LandOwnerName;

	UPROPERTY(Replicated)
	double ExpiresAt = 0.0;

	FName AppliedItem;
};
