#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SBox;

/** Le livre des breuvages : une page d'introduction, puis une page par breuvage (ingrédients par litre, chaleur, temps). */
class SBookPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBookPanel) : _Controller(nullptr) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void ShowPage(int32 NewPage);
	TSharedRef<SWidget> BuildPage() const;
	int32 PageCount() const;

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SBox> Body;
	int32 Page = 0;
};
