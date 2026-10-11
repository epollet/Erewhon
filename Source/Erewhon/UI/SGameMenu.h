#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SBox;

/**
 * Menu Échap : reprendre, options graphiques, quitter.
 * Le monde continue de tourner (jeu multijoueur) ; seul le joueur est en pause.
 */
class SGameMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGameMenu) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void ShowMain();
	void ShowOptions();

	/** Une ligne d'option : libellé, valeur, et flèches pour la changer. */
	TSharedRef<SWidget> OptionRow(const FText& Label, TFunction<FText()> Value, TFunction<void(int32)> Step);

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SBox> Body;
	bool bShowingOptions = false;
};
