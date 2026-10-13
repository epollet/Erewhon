#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Inventory/InventoryComponent.h"

class ABackpack;
class AErewhonPlayerController;
class SBox;
class UBackpackComponent;
class UInventoryComponent;

/**
 * Inventaire en trois colonnes (Inventory / Clothes / Equipment), une ligne
 * « Base » puis une ligne par vêtement qui apporte des poches — disposition de 3DSimEco.
 * Clic sur un objet : équiper / ranger.
 *
 * Avec le sac à dos posé et ouvert (E en le visant), une quatrième colonne montre son contenu :
 * on y glisse-dépose les objets dans les deux sens, un clic dans le sac reprend l'objet.
 * A au survol d'un objet : le jeter (confirmation pour un outil).
 */
class SInventoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInventoryPanel) : _Controller(nullptr), _Bag(nullptr), _Storage(nullptr), _Shelves(nullptr), _ShelfIndex(0) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
		/** Sac posé au sol qu'on vient d'ouvrir (nul : l'inventaire seul). */
		SLATE_ARGUMENT(ABackpack*, Bag)
		/** Ou une cagette, un tonneau qu'on vient d'ouvrir. */
		SLATE_ARGUMENT(class AStorageActor*, Storage)
		/** Ou une étagère d'une apothicairerie (ShelfIndex) : ses quatre étages, de haut en bas. */
		SLATE_ARGUMENT(class AApothecary*, Shelves)
		SLATE_ARGUMENT(int32, ShelfIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void Rebuild();
	TSharedRef<SWidget> BuildSlot(bool bBag, int32 SlotIndex, const FLinearColor& Accent, const FText& EmptyLabel) const;
	TSharedRef<SWidget> BuildBagColumn() const;
	UInventoryComponent* GetInventory() const;
	UBackpackComponent* GetBackpack() const;
	bool ShowsBag() const;
	/** Les slots de l'autre côté de l'échange : le sac posé, ou le conteneur ouvert. */
	const TArray<FInventorySlot>& OtherSlots() const;
	/** Déplace un objet entre ses poches et l'autre côté (To = INDEX_NONE : là où il y a de la place). */
	void MoveItem(bool bFromOther, int32 From, bool bToOther, int32 To) const;

	void Discard(bool bBag, int32 SlotIndex);

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TWeakObjectPtr<ABackpack> OpenedBag;
	TWeakObjectPtr<class AStorageActor> OpenedStorage;
	TWeakObjectPtr<class AApothecary> OpenedShelves;
	int32 ShelfIndex = 0;
	TSharedPtr<SBox> Body;

	/** Les cases affichées, pour savoir laquelle est survolée (touche A). */
	struct FSlotRef { TWeakPtr<SWidget> Widget; bool bBag = false; int32 Index = INDEX_NONE; };
	mutable TArray<FSlotRef> SlotWidgets;

	/** Outil en attente de confirmation avant d'être jeté. */
	TOptional<FSlotRef> PendingDiscard;
};
