#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SBox;

/**
 * Menu Échap : reprendre, options, quitter. Les options sont rangées en onglets.
 * Le monde continue de tourner (jeu multijoueur) ; seul le joueur est en pause.
 */
class SGameMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGameMenu) : _Controller(nullptr) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Captures automatiques : ouvre directement les options. */
	void ShowOptionsForTest() { Show(EPage::Display); }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	enum class EPage : uint8 { Main, Display, World, Sound };

	void Show(EPage Page);
	TSharedRef<SWidget> BuildMain();
	TSharedRef<SWidget> BuildOptions();

	/** Une ligne d'option : libellé, aide, et sélecteur ‹ valeur ›. */
	TSharedRef<SWidget> OptionRow(const FText& Label, const FText& Help, TFunction<FText()> Value, TFunction<void(int32)> Step);

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SBox> Body;
	EPage Page = EPage::Main;
};
