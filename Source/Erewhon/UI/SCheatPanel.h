#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SEditableTextBox;

/**
 * Panneau de triche (Ctrl+Alt+ù puis Ctrl+Maj+*) : avancer le temps, fixer ses pièces.
 * Le serveur n'accepte ces commandes que de l'hôte, ou dans un build de développement.
 */
class SCheatPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCheatPanel) : _Controller(nullptr) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	FText ClockText() const;
	void SetCoins();

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SEditableTextBox> CoinsBox;
};
