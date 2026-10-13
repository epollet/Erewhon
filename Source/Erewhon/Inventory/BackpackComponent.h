#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/InventoryComponent.h"
#include "BackpackComponent.generated.h"

class ABackpack;
class APawn;

/**
 * Le sac à dos d'un joueur, porté par son PlayerState : 12 slots de rangement et 2 slots
 * d'outils (accrochés de chaque côté du sac, visibles de tous). Le sac est soit sur le dos,
 * soit posé au sol (un ABackpack). Toutes les modifications passent par le serveur.
 */
UCLASS(ClassGroup = (Erewhon))
class EREWHON_API UBackpackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr int32 StorageSlots = 12;
	static constexpr int32 ToolSlots = 2;

	UBackpackComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool IsOwned() const { return bOwned; }
	bool IsWorn() const { return bOwned && bWorn; }
	ABackpack* GetDropped() const { return Dropped; }
	const TArray<FInventorySlot>& GetSlots() const { return Slots; }

	/** Outils accrochés aux côtés du sac (vus de tous). */
	const TArray<FName>& GetSideTools() const { return SideTools; }

	/** Exemplaires rangés (hors slots d'outils). */
	int32 CountStored(FName ItemId) const;

	/** Le sac est posé à moins de Reach du pion (sur le dos, on ne peut pas y fouiller). */
	bool IsAccessibleFrom(const APawn* Pawn, float Reach = 400.f) const;

	// ── Serveur ──

	/** Achat : un sac neuf, directement sur le dos. */
	void Grant();

	/** Retour d'un joueur : son sac tel qu'il l'a laissé (porté, ou posé à DroppedAt). */
	void Restore(bool bInOwned, bool bInWorn, const TArray<FInventorySlot>& Saved, const FTransform& DroppedAt);

	/** Range dans les slots de rangement (les outils d'abord dans leurs slots s'il y a de la place). */
	int32 StoreItem(FName ItemId, int32 Count, bool bToolSlotsFirst = false);
	bool RemoveStored(FName ItemId, int32 Count);

	/** Pose le sac au sol devant le pion, ou le remet sur le dos. */
	bool Drop(const APawn* Pawn);
	bool PickUp();

	TArray<FInventorySlot>& EditSlots() { return Slots; }
	void Commit();

	FOnInventoryChanged OnBackpackChanged;

private:
	void EnsureSlots();
	ABackpack* SpawnDropped(const FTransform& Transform);
	const FString& OwnerKey() const;

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bOwned = false;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bWorn = false;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	TObjectPtr<ABackpack> Dropped;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	TArray<FName> SideTools;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	TArray<FInventorySlot> Slots;
};
