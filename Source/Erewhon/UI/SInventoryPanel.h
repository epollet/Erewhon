#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

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
 * Avec le sac à dos (porté, ou posé et ouvert avec F), une quatrième colonne montre son
 * contenu : on y glisse-dépose les objets dans les deux sens, un clic dans le sac reprend l'objet.
 */
class SInventoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInventoryPanel) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
		/** Sac posé au sol qu'on vient d'ouvrir (nul : seulement le sac porté, s'il y en a un). */
		SLATE_ARGUMENT(ABackpack*, Bag)
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

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TWeakObjectPtr<ABackpack> OpenedBag;
	TSharedPtr<SBox> Body;
};
