#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ABhurak;
class AErewhonPlayerController;
class SBox;

/** Le harnachement du bhurak : le harnais, puis ses trois places (flanc gauche, dos, flanc droit). */
class SBhurakPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBhurakPanel) : _Controller(nullptr), _Bhurak(nullptr) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
		SLATE_ARGUMENT(ABhurak*, Bhurak)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void Rebuild();

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TWeakObjectPtr<ABhurak> Bhurak;
	TSharedPtr<SBox> Body;
};
