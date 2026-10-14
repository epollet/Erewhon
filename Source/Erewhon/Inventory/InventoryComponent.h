#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/ItemTypes.h"
#include "InventoryComponent.generated.h"

struct FItemDef;
class UItemDatabase;

UENUM(BlueprintType)
enum class ESlotKind : uint8
{
	Inventory,  // bleu : objets quelconques
	Equipment,  // orange : outils équipés, le premier est « Mains »
	Clothing,   // vert : un vêtement de la partie du corps indiquée
};

USTRUCT(BlueprintType)
struct FInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName ItemId;

	UPROPERTY(BlueprintReadOnly)
	int32 Count = 0;

	UPROPERTY(BlueprintReadOnly)
	ESlotKind Kind = ESlotKind::Inventory;

	/** Pour un slot de vêtement : la partie du corps qu'il habille. */
	UPROPERTY(BlueprintReadOnly)
	EClothingSlot BodyPart = EClothingSlot::None;

	/** Vêtement qui apporte ce slot (poche) — None pour un slot de base. */
	UPROPERTY(BlueprintReadOnly)
	EClothingSlot ProvidedBy = EClothingSlot::None;

	bool IsEmpty() const { return Count <= 0 || ItemId.IsNone(); }
	void Clear() { ItemId = NAME_None; Count = 0; }
};

/** Ce que les autres joueurs voient : objet en main et vêtements portés. */
USTRUCT(BlueprintType)
struct FInventoryAppearance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName HandItem;

	UPROPERTY(BlueprintReadOnly)
	FName TorsoItem;

	UPROPERTY(BlueprintReadOnly)
	FName LegsItem;

	/** L'emplacement de la barre qui est en main (0, 1 ou 2). */
	UPROPERTY(BlueprintReadOnly)
	uint8 ActiveHotbar = 0;

	/** Ce que portent les autres emplacements de la barre (outils dans le dos, le reste à la ceinture) ; rien pour l'actif. */
	UPROPERTY(BlueprintReadOnly)
	TArray<FName> Stowed;

	bool operator==(const FInventoryAppearance& Other) const
	{
		return HandItem == Other.HandItem && TorsoItem == Other.TorsoItem && LegsItem == Other.LegsItem
			&& ActiveHotbar == Other.ActiveHotbar && Stowed == Other.Stowed;
	}
};

DECLARE_MULTICAST_DELEGATE(FOnInventoryChanged);

/**
 * Inventaire d'un joueur, porté par son PlayerState pour survivre au respawn.
 * Les slots ne sont répliqués qu'au propriétaire ; l'apparence l'est à tous.
 * Toutes les modifications passent par le serveur.
 */
UCLASS(ClassGroup = (Erewhon), meta = (BlueprintSpawnableComponent))
class EREWHON_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const TArray<FInventorySlot>& GetSlots() const { return Slots; }
	const FInventoryAppearance& GetAppearance() const { return Appearance; }
	FName GetHandItem() const { return Appearance.HandItem; }

	// ── La barre : les emplacements d'équipement de base ──

	int32 GetHotbarSize() const;
	int32 GetActiveHotbar() const { return Appearance.ActiveHotbar; }
	/** Index dans les slots du N-ième emplacement de la barre (INDEX_NONE s'il n'existe pas). */
	int32 HotbarSlotIndex(int32 Hotbar) const;
	/** Le slot tenu en main : l'emplacement actif de la barre. */
	int32 HandSlotIndex() const { return HotbarSlotIndex(ActiveHotbar); }
	FInventorySlot* EditHandSlot() { return Slots.IsValidIndex(HandSlotIndex()) ? &Slots[HandSlotIndex()] : nullptr; }

	/** Serveur : choisir l'emplacement en main. */
	void SelectHotbar(int32 Hotbar);

	UFUNCTION(Server, Reliable)
	void ServerSelectHotbar(int32 Hotbar);

	/** Glisser un objet d'une case à une autre de ses poches ou de sa barre (un outil, un objet qui se tient, dans la barre). */
	UFUNCTION(Server, Reliable)
	void ServerMoveSlot(int32 From, int32 To);

	/** Nombre d'exemplaires rangés dans les slots d'inventaire (hors équipement). */
	int32 CountStored(FName ItemId) const;

	/** Possédé n'importe où (rangé, équipé ou porté). */
	bool Owns(FName ItemId) const;

	bool CanStore(FName ItemId, int32 Count) const;

	/** Poids de tout ce qu'on porte sur soi : poches, mains, barre, vêtements (kg). */
	float GetTotalWeight() const;

	// ── Serveur ──

	void ResetToDefault();

	/** Restaure un inventaire sauvegardé ; revient au défaut s'il est incohérent. */
	void RestoreSlots(const TArray<FInventorySlot>& Saved);

	/** Ajoute dans les slots d'inventaire, renvoie le nombre réellement rangé. */
	int32 StoreItem(FName ItemId, int32 Count);

	/** Retire des slots d'inventaire uniquement. */
	bool RemoveStored(FName ItemId, int32 Count);

	/**
	 * Clic sur un slot : équipe un outil ou un vêtement rangé, ou range un objet équipé.
	 * Renvoie un message d'erreur à afficher, vide en cas de succès.
	 */
	FText UseSlot(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerUseSlot(int32 SlotIndex);

	/** Accès direct du serveur (échanges avec le sac à dos), suivi de CommitChanges. */
	TArray<FInventorySlot>& EditSlots() { return Slots; }
	void CommitChanges() { Commit(); }

	UFUNCTION(Client, Reliable)
	void ClientInventoryMessage(const FText& Message);

	FOnInventoryChanged OnInventoryChanged;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnInventoryMessage, const FText&);
	FOnInventoryMessage OnInventoryMessage;

private:
	const FItemDef* FindDef(FName ItemId) const;
	bool IsStructureValid(const TArray<FInventorySlot>& Candidate) const;
	int32 FindFreeSlot(ESlotKind Kind, EClothingSlot ProvidedByToAvoid = EClothingSlot::None) const;
	int32 FindClothingSlot(EClothingSlot BodyPart) const;
	bool ArePocketsEmpty(EClothingSlot BodyPart) const;
	void AddPockets(EClothingSlot BodyPart, const FItemDef& Clothing);
	void RemovePockets(EClothingSlot BodyPart);
	FText EquipFrom(int32 SlotIndex, const FItemDef& Def);
	FText UnequipFrom(int32 SlotIndex, const FItemDef& Def);
	void Commit();

	UFUNCTION()
	void OnRep_Slots();

	UFUNCTION()
	void OnRep_Appearance();

	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<FInventorySlot> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance)
	FInventoryAppearance Appearance;

	/** Serveur : l'emplacement actif de la barre. */
	int32 ActiveHotbar = 0;
};
