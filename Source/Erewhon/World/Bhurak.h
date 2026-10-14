#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Inventory/InventoryComponent.h"
#include "Player/Interactable.h"
#include "Bhurak.generated.h"

class ACart;
class AStorageActor;
class UAudioComponent;
class UBoxComponent;
class UStaticMeshComponent;

/**
 * Le bhurak : un gros bovin poilu, un peu plus haut qu'un homme (1,80 m), qu'on achète au General Store
 * et qu'on pose comme une charrette. Harnaché, il porte deux sacoches sur les flancs et une sacoche ou
 * une selle sur le dos ; sellé, on le monte (+50 % de vitesse) ; harnaché, il tire une charrette.
 *
 * Un pion sans contrôleur : monté, il suit son cavalier (dont la capsule fait le mouvement) ; attelée,
 * la charrette le suit comme elle suit qui la conduit (voir ACart::FollowDriver).
 */
UCLASS()
class EREWHON_API ABhurak : public APawn, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	/** Les places du harnais : flanc gauche, dos, flanc droit. */
	static constexpr int32 GearSlots = 3;
	static constexpr int32 BackSlot = 1;
	static constexpr float Height = 180.f;
	/** L'assise de la selle, au-dessus des sabots. */
	static constexpr float SeatHeight = 160.f;
	/** Le cavalier s'élève d'autant (son bassin, assis, est à ~45 cm de ses pieds). */
	static constexpr float RideRise = SeatHeight - 45.f;
	static constexpr float RideSpeedFactor = 1.5f;

	inline static const FName ItemId = TEXT("bhurak");
	inline static const FName HarnessItem = TEXT("harness");
	inline static const FName SaddleItem = TEXT("saddle");
	inline static const FName SaddlebagItem = TEXT("saddlebag");

	ABhurak();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostNetReceiveLocationAndRotation() override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(class AErewhonPlayerController* User) override;

	bool IsOwnedBy(const FString& Key) const { return !Key.IsEmpty() && Key == OwnerKey; }
	const FString& GetOwnerKey() const { return OwnerKey; }
	const FString& GetOwnerName() const { return OwnerName; }

	bool HasHarness() const { return bHarness; }
	FName GetGear(int32 Slot) const { return Gear.IsValidIndex(Slot) ? Gear[Slot] : NAME_None; }
	bool HasSaddle() const { return GetGear(BackSlot) == SaddleItem; }
	bool HasAnyGear() const { return Gear.ContainsByPredicate([](const FName& G) { return !G.IsNone(); }); }
	AStorageActor* GetBag(int32 Slot) const { return Bags.IsValidIndex(Slot) ? Bags[Slot].Get() : nullptr; }
	APawn* GetRider() const { return Rider; }
	ACart* GetTowedCart() const { return TowedCart; }

	/** Le bout des brancards d'une charrette attelée (monde) : sous l'arrière du bhurak. */
	FVector GetHitchPoint() const;
	float GetHalfLength() const { return HalfLength; }
	float GetHalfWidth() const { return HalfWidth; }

	/** Où accrocher la sacoche d'une place (repère du bhurak). */
	FTransform GearTransform(int32 Slot) const;

	/** Le modèle du bhurak d'une robe (0 brune, 1 crème, 2 charbon) et sa mise à l'échelle (1,80 m, tête vers +X). */
	static class UStaticMesh* Mesh(FTransform& OutFit, uint8 Coat = 0);
	static constexpr int32 CoatCount = 3;
	/** L'objet de l'inventaire d'une robe, et la robe d'un objet (INDEX_NONE si ce n'est pas un bhurak). */
	static FName CoatItem(uint8 Coat);
	static int32 CoatOfItem(FName Item);
	uint8 GetCoat() const { return Coat; }

	// ── Serveur ──

	void Setup(const FString& InOwnerKey, const FString& InOwnerName, uint8 InCoat = 0);
	void SetHarness(bool bOn);
	void SetGear(int32 Slot, FName Item, AStorageActor* Bag);
	void SetRider(APawn* Pawn);
	void SetTowedCart(ACart* Cart);

	/** Le harnais, la selle ou une sacoche a changé (panneau ouvert). */
	FOnInventoryChanged OnGearChanged;

private:
	UFUNCTION()
	void OnRep_Gear();

	UFUNCTION()
	void OnRep_Rider();

	void ApplyGear();
	/** Le cavalier ne bute ni contre sa monture, ni contre ses sacoches, ni contre la charrette attelée. */
	void ApplyRiderCollision(APawn* Pawn, bool bIgnore);
	void FollowRider(float DeltaSeconds);
	void Animate(float DeltaSeconds);
	float GroundAt(const FVector& World) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	/** Le corps, qu'on fait doucement onduler (pas, respiration). */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> BodyPivot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> GearParts;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Voice;

	UPROPERTY(Replicated)
	FString OwnerKey;

	UPROPERTY(Replicated)
	FString OwnerName;

	UPROPERTY(Replicated)
	uint8 Coat = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Gear)
	bool bHarness = false;

	UPROPERTY(ReplicatedUsing = OnRep_Gear)
	TArray<FName> Gear;

	/** Les sacoches : des conteneurs accrochés au bhurak (nuls pour une selle ou une place vide). */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<AStorageActor>> Bags;

	UPROPERTY(ReplicatedUsing = OnRep_Rider)
	TObjectPtr<APawn> Rider;

	UPROPERTY(Replicated)
	TObjectPtr<ACart> TowedCart;

	TWeakObjectPtr<APawn> AppliedRider;

	float HalfLength = 120.f;
	float HalfWidth = 50.f;
	float WalkPhase = 0.f;
	float Speed = 0.f;
	float Yaw = 0.f;
	FVector LastLocation = FVector::ZeroVector;
	double NextGrunt = 0.0;

	/** Son attente, à l'arrêt : tirée au hasard quand la précédente se termine. */
	enum class EIdle : uint8 { Stand, Graze, LookLeft, LookRight, Shake, ShiftWeight };
	EIdle Idle = EIdle::Stand;
	double IdleStart = 0.0;
	double IdleEnd = 0.0;
	/** Pose d'attente adoucie : lacet, tangage (vers l'avant), roulis. */
	FRotator IdlePose = FRotator::ZeroRotator;
};
