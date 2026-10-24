#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SEditableTextBox;

/**
 * Panneau de triche (Ctrl+Alt+ù puis Ctrl+Maj+*) : avancer le temps, choisir la météo, fixer ses pièces.
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

	/** Le temps qu'il fait (EKind + 1), 0 sans état du jeu ; bOutForced : imposé par la triche. */
	uint8 CurrentWeather(bool& bOutForced) const;
	/** Libellé de la liste déroulante : le temps qu'il fait, naturel ou forcé (suit l'horloge). */
	FText WeatherLabel() const;
	/** Le menu de la liste : les sept types de temps puis « Naturelle », le choix en cours surligné. */
	TSharedRef<SWidget> WeatherChoices();

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SEditableTextBox> CoinsBox;
	TSharedPtr<class SMenuAnchor> WeatherMenu;
};
