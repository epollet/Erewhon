#pragma once

#include "CoreMinimal.h"
#include "Core/ErewhonReport.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SBox;
class SEditableTextBox;
class SMultiLineEditableTextBox;
struct FSlateDynamicImageBrush;

/**
 * Popup de signalement (AltGr+²) : catégorie, titre, description, et l'aperçu de l'écran
 * capturé à l'appui. Enregistre le cas dans Reports/ (voir ErewhonReport).
 */
class SReportPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SReportPanel) : _Controller(nullptr) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, ErewhonReport::FCapture InCapture);

	/** Le champ qui reçoit le clavier à l'ouverture. */
	TSharedRef<SWidget> GetFocusTarget() const;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TSharedRef<SWidget> CategoryButtons();
	void SetCategory(ErewhonReport::ECategory InCategory);
	void Submit();
	void Cancel();

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	ErewhonReport::FCapture Capture;
	ErewhonReport::ECategory Category = ErewhonReport::ECategory::Bug;
	bool bWithScreenshot = true;

	TSharedPtr<SBox> CategoryRow;
	TSharedPtr<SEditableTextBox> TitleBox;
	TSharedPtr<SMultiLineEditableTextBox> DescriptionBox;
	TSharedPtr<FSlateDynamicImageBrush> Preview;
};
