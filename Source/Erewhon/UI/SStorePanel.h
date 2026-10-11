#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AErewhonPlayerController;
class SBox;

/**
 * Boutique du marchand, en trois onglets :
 * - Secteurs : une carte des environs de la cabane ; on choisit une case, on l'achète ou la revend ;
 * - Acheter : outils et vêtements en cartes illustrées ;
 * - Vendre : les objets rangés, à l'unité ou en totalité.
 */
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
	TSharedRef<SWidget> BuildSectorDetail();
	TSharedRef<SWidget> BuildBuy();
	TSharedRef<SWidget> BuildSell();

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SBox> Body;
	ETab Tab = ETab::Sell;
	FIntPoint Selected = FIntPoint(1, 1);
};
