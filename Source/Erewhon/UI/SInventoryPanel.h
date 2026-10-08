#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SBox;
class UInventoryComponent;

/**
 * Inventaire en trois colonnes (Inventory / Clothes / Equipment), une ligne
 * « Base » puis une ligne par vêtement qui apporte des poches — disposition de 3DSimEco.
 * Clic sur un objet : équiper / ranger.
 */
class SInventoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInventoryPanel) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void Rebuild();
	TSharedRef<SWidget> BuildSlot(int32 SlotIndex, const FLinearColor& Accent, const FText& EmptyLabel) const;
	UInventoryComponent* GetInventory() const;

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SBox> Body;
};
