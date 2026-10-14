#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/InventoryComponent.h"
#include "Player/Interactable.h"
#include "Storage.generated.h"

class ACart;
class UStaticMesh;
class UStaticMeshComponent;

/** Les conteneurs et le mobilier qu'on pose dans le monde (l'ordre est sauvegardé : ajouter à la fin). */
UENUM()
enum class EStorageKind : uint8
{
	Crate,    // cagette : des plantes, 12 places, s'empile par 3
	Barrel,   // tonneau : des plantes, 30 places, lourd à porter
	Shelf,    // étagère à potions : 4 étages, une pile de 10 par étage
	Table,    // table de travail : on y pose un mortier
	Mortar,   // mortier : plantes → écrasés
	Cauldron, // chaudron : pendu à la crémaillère d'une cheminée allumée, écrasés → potions
	DryingRack, // séchoir : les plantes y sèchent
	Workbench,  // petit atelier d'apothicaire : pots-pourris, teintures, bouquets
	Potpourri,  // pot-pourri posé : parfume les alentours et apaise ceux qui passent, un temps
};

/** Ce qu'on peut poser dans le monde depuis ses mains (objet de l'inventaire). */
namespace ErewhonPlaceables
{
	/** Le type de conteneur que représente un objet de l'inventaire (« crate », « barrel »), s'il en est un. */
	EREWHON_API TOptional<EStorageKind> StorageForItem(FName ItemId);
	EREWHON_API FName ItemForStorage(EStorageKind Kind);
	EREWHON_API bool IsCart(FName ItemId);

	EREWHON_API int32 Capacity(EStorageKind Kind);
	/** Mobilier de l'apothicairerie (étagère, table, mortier, chaudron) : ni pile ni charrette. */
	EREWHON_API bool IsFurniture(EStorageKind Kind);
	/** On avance moitié moins vite en le portant. */
	EREWHON_API bool IsHeavy(EStorageKind Kind);
	EREWHON_API float Height(EStorageKind Kind);
	EREWHON_API FText KindName(EStorageKind Kind);

	/** Le modèle d'un conteneur ou de la charrette, et sa mise à l'échelle (posé sur Z = 0, grand côté sur Y). */
	EREWHON_API UStaticMesh* StorageMesh(EStorageKind Kind, FTransform& OutFit);
	EREWHON_API UStaticMesh* CartMesh(FTransform& OutFit);

	/**
	 * Peut-on poser ceci là ? (Type : 0 au sol, 1 sur un autre, 2 dans une charrette.)
	 * La charrette se pose partout ; une cagette ou un tonneau va dans une charrette partout ;
	 * sinon, conteneurs et mobilier ne se posent que sur ses propres secteurs.
	 */
	EREWHON_API bool CanPlaceAt(const UObject* WorldContext, const FString& PlayerKey, bool bCart, EStorageKind Kind, uint8 Type, const FVector& Location);

	/** L'eau : un tonneau en tient 100 L (et rien d'autre tant qu'il en reste), le chaudron 10 L, une préparation au chaudron en boit 0,15 L. */
	constexpr float WaterBarrelLiters = 100.f;
	constexpr float CauldronLiters = 10.f;
	constexpr float WaterPerBrew = 0.15f;

	/** Temps de séchage d'une plante sur le séchoir, durée de vie d'un pot-pourri posé et de sa sérénité (s). */
	constexpr double DryingSeconds = 240.0;
	constexpr double PotpourriSeconds = 600.0;
	constexpr double CalmSeconds = 300.0;
	constexpr float PotpourriRadius = 600.f;

	/** Un tonneau posé près du bec de la fontaine s'y cale : sa place exacte. */
	EREWHON_API TOptional<FVector> FountainSnap(const UObject* WorldContext, EStorageKind Kind, const FVector& Location);

	/** Un chaudron posé près d'une cheminée s'accroche à sa crémaillère : le repère du crochet. */
	EREWHON_API TOptional<FTransform> HookFor(const UObject* WorldContext, EStorageKind Kind, const FVector& Location);

	/** Nombre de cagettes au plus dans une pile. */
	constexpr int32 MaxStack = 3;
}

/**
 * Une cagette ou un tonneau posé dans le monde, avec son contenu. On le prend dans ses mains
 * (E), on le repose d'un clic (au sol, sur une autre cagette, dans la charrette), on l'ouvre (F).
 * Seul son propriétaire peut le déplacer ou l'ouvrir.
 */
UCLASS()
class EREWHON_API AStorageActor : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	AStorageActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	EStorageKind GetKind() const { return Kind; }
	/** Séchoir : l'horloge du monde à laquelle chaque place a reçu ses plantes (0 : vide ou sec). */
	const TArray<double>& GetSlotTimes() const { return SlotTimes; }
	/** Pot-pourri : l'horloge du monde à laquelle il s'éteint. */
	double GetExpiresAt() const { return ExpiresAt; }
	/** Tonneau d'eau, chaudron : les litres d'eau qu'il contient. */
	float GetWater() const { return Water; }
	bool IsWaterBarrel() const { return Kind == EStorageKind::Barrel && Water > 0.01f; }
	bool IsOwnedBy(const FString& Key) const { return !Key.IsEmpty() && Key == OwnerKey; }
	const FString& GetOwnerKey() const { return OwnerKey; }
	const FString& GetOwnerName() const { return OwnerName; }
	const TArray<FInventorySlot>& GetSlots() const { return Slots; }
	APawn* GetCarrier() const { return Carrier; }
	ACart* GetCart() const { return Cart; }
	int32 GetCartSlot() const { return CartSlot; }
	AStorageActor* GetBelow() const { return Below; }
	AStorageActor* GetAbove() const { return Above; }

	/** Le haut de la pile dont fait partie ce conteneur, et sa hauteur (1 à 3). */
	AStorageActor* TopOfStack();
	int32 StackSize() const;

	/** Peut-on ranger cet objet dedans ? (des plantes ; sur une étagère, potions et écrasés) */
	bool Accepts(const struct FItemDef& Def) const;
	/** Combien d'exemplaires par place (10 sur une étagère). */
	int32 SlotCap(const struct FItemDef& Def) const;
	int32 CountStored(FName ItemId) const;

	// ── Serveur ──

	void Setup(EStorageKind InKind, const FString& InOwnerKey, const FString& InOwnerName);
	void SetSlotTimes(const TArray<double>& Times) { SlotTimes = Times; }
	void SetExpiresAt(double Time) { ExpiresAt = Time; }
	void SetWater(float Liters);
	TArray<FInventorySlot>& EditSlots() { return Slots; }
	void Commit();
	int32 StoreItem(FName ItemId, int32 Count);

	/** Se détache de tout (sol, pile, charrette, mains) avant d'aller ailleurs. */
	void Release();
	void PlaceOnGround(const FVector& Location, float Yaw);
	bool StackOn(AStorageActor* Base);
	bool LoadInto(ACart* InCart, int32 Slot);
	void CarryBy(APawn* Pawn);

	FOnInventoryChanged OnStorageChanged;

private:
	void ApplyCollision();

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void OnRep_Slots() { Commit(); }

	UFUNCTION()
	void OnRep_Water() { OnStorageChanged.Broadcast(); }

	UPROPERTY(ReplicatedUsing = OnRep_Water)
	float Water = 0.f;

	/** Ce que montre le meuble : les flacons de l'étagère, les bouquets du séchoir. */
	void RefreshDisplay();
	/** Séchoir (serveur) : suit l'arrivée des plantes, change en plantes sèches celles qui ont assez séché. */
	void UpdateDrying();
	/** Pot-pourri : les pétales qui flottent (client), la sérénité des voisins et l'extinction (serveur). */
	void UpdatePotpourri(float DeltaSeconds);

	UPROPERTY(Replicated)
	TArray<double> SlotTimes;

	/** L'objet que suit chaque place du séchoir (une autre plante remet le séchage à zéro). */
	TArray<FName> TimedItems;

	UPROPERTY(Replicated)
	double ExpiresAt = 0.0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Petals;
	float PetalTime = 0.f;
	float CalmTimer = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> DisplayFrame;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> DisplayParts;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Replicated)
	EStorageKind Kind = EStorageKind::Crate;

	UPROPERTY(Replicated)
	FString OwnerKey;

	UPROPERTY(Replicated)
	FString OwnerName;

	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<FInventorySlot> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	TObjectPtr<APawn> Carrier;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	TObjectPtr<ACart> Cart;

	UPROPERTY(Replicated)
	int32 CartSlot = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	TObjectPtr<AStorageActor> Below;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	TObjectPtr<AStorageActor> Above;
};

/**
 * La charrette : deux roues, deux brancards, et deux emplacements pour des conteneurs.
 * Devant ses brancards, F pour la conduire (on avance moitié moins vite) ; F encore pour la lâcher.
 */
UCLASS()
class EREWHON_API ACart : public AActor, public IErewhonInteractable
{
	GENERATED_BODY()

public:
	static constexpr int32 SlotCount = 2;

	ACart();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostNetReceiveLocationAndRotation() override;

	virtual FText GetInteractionLabel() const override;
	virtual void Interact(AErewhonPlayerController* Controller) override;

	bool IsOwnedBy(const FString& Key) const { return !Key.IsEmpty() && Key == OwnerKey; }
	const FString& GetOwnerKey() const { return OwnerKey; }
	const FString& GetOwnerName() const { return OwnerName; }
	APawn* GetDriver() const { return Driver; }

	/** Repère local : l'essieu (milieu entre les roues) et le bout des brancards, sur l'axe X. */
	static constexpr float AxleX = 9.f;
	static constexpr float GripX = -140.f;
	/** Les roues : diamètre, écartement (demi-voie), hauteur de l'essieu. */
	static constexpr float WheelDiameter = 92.f;
	static constexpr float WheelHalfTrack = 75.f;
	static constexpr float AxleZ = WheelDiameter * 0.5f;
	AStorageActor* GetLoad(int32 Slot) const { return Loads.IsValidIndex(Slot) ? Loads[Slot].Get() : nullptr; }

	/** Repère (local) d'un emplacement de la plate-forme. */
	static FTransform SlotTransform(int32 Slot);

	/** L'emplacement libre le plus proche d'un point monde, ou INDEX_NONE. */
	int32 FreeSlotNear(const FVector& World) const;

	/** Le joueur se tient-il devant les brancards (pour prendre la charrette) ? */
	bool IsAtHandles(const FVector& World) const;

	// ── Serveur ──

	void Setup(const FString& InOwnerKey, const FString& InOwnerName);
	void SetLoad(int32 Slot, AStorageActor* Storage);
	void StartDriving(APawn* Pawn);
	void StopDriving();

private:
	UFUNCTION()
	void OnRep_Driver();

	void ApplyDriverCollision(APawn* Pawn, bool bIgnore);
	/** La charrette suit son conducteur : les brancards dans ses mains, elle pivote autour de l'essieu. */
	void FollowDriver();
	float GroundAt(const FVector& World) const;
	/** Les roues tournent selon le chemin parcouru par chacune (en pivotant, l'une avance, l'autre recule). */
	void SpinWheels();

	/** Pivot de chaque roue (à l'essieu), qu'on fait tourner autour de l'axe Y. */
	TArray<TWeakObjectPtr<USceneComponent>> WheelPivots;
	TArray<FVector> LastWheelPositions;
	TArray<float> WheelAngles;

	/** Le roulement des roues, plus fort quand elle va vite. */
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> RollAudio;
	FVector LastRollLocation = FVector::ZeroVector;
	float RollSpeed = 0.f;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Replicated)
	FString OwnerKey;

	UPROPERTY(Replicated)
	FString OwnerName;

	UPROPERTY(ReplicatedUsing = OnRep_Driver)
	TObjectPtr<APawn> Driver;

	TWeakObjectPtr<APawn> AppliedDriver;

	UPROPERTY(Replicated)
	TArray<TObjectPtr<AStorageActor>> Loads;
};
