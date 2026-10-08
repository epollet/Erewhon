#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SBox;

/** Boutique du marchand : onglets Secteurs, Acheter, Vendre. */
class SStorePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStorePanel) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	enum class ETab : uint8 { Sectors, Buy, Sell };

	void Rebuild();
	void OnSectorOwnerChanged(const FIntPoint& Sector) { Rebuild(); }
	TSharedRef<SWidget> BuildTabs();
	TSharedRef<SWidget> BuildSectors();
	TSharedRef<SWidget> BuildBuy();
	TSharedRef<SWidget> BuildSell();

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SBox> Body;
	ETab Tab = ETab::Sell;
};
