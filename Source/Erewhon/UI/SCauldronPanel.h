#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class AStorageActor;
class SBox;

/**
 * Le chaudron : son eau (versée au pichet ou puisée au tonneau), le feu dessous, la hauteur du support
 * et la température qu'elle donne, les ingrédients qu'on y jette, et le breuvage qui prend quand tout est juste.
 * Rien ne dit quelle recette on prépare : on découvre, ou on lit le livre des breuvages.
 */
class SCauldronPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCauldronPanel) : _Controller(nullptr), _Cauldron(nullptr) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
		SLATE_ARGUMENT(AStorageActor*, Cauldron)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void Rebuild();
	TSharedRef<SWidget> BuildGauges() const;
	TSharedRef<SWidget> BuildLiquid() const;
	TSharedRef<SWidget> BuildIngredients() const;

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TWeakObjectPtr<AStorageActor> Cauldron;
	TSharedPtr<SBox> Body;
};
