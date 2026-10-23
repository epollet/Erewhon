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
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FText ClockText() const;
	void SetCoins();

	/** Le temps qu'il fait (EKind + 1), 0 si la météo est naturelle et l'état du jeu absent. */
	uint8 CurrentWeather(bool& bOutForced) const;
	/** Un bouton par type de temps, celui en cours surligné, et « Naturelle » surligné hors triche. */
	TSharedRef<SWidget> WeatherButtons() const;

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SEditableTextBox> CoinsBox;
	TSharedPtr<class SBox> WeatherRow;
	/** Ce que montrent les boutons : on les refait quand le temps change. */
	uint8 ShownWeather = 0;
	bool bShownForced = false;
};
