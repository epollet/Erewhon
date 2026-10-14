#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "World/SectorTypes.h"

class AErewhonPlayerController;
class SBox;

/**
 * Comptoir d'un commerce de la place :
 * - General Store, en deux onglets : Acheter (outils et vêtements en cartes illustrées) et
 *   Vendre (les objets rangés, à l'unité ou en totalité) ;
 * - Sector Manager : une carte des environs ; on choisit une case, on l'achète ou la revend.
 */
class SStorePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStorePanel) : _Controller(nullptr), _Kind(EShopKind::GeneralStore), _bVillageTab(false) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
		SLATE_ARGUMENT(EShopKind, Kind)
		/** Chez le gérant : ouvrir directement l'onglet du village. */
		SLATE_ARGUMENT(bool, bVillageTab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	enum class ETab : uint8 { Sectors, Village, Class, Buy, Sell };

	void Rebuild();
	void OnSectorOwnerChanged(const FIntPoint& Sector) { Rebuild(); }
	TSharedRef<SWidget> BuildTabs();
	TSharedRef<SWidget> BuildSectors();
	TSharedRef<SWidget> BuildSectorDetail();
	TSharedRef<SWidget> BuildBuy();
	TSharedRef<SWidget> BuildSell();
	TSharedRef<SWidget> BuildVillage();
	TSharedRef<SWidget> BuildClass();
	void OnVillageChanged() { Rebuild(); }

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SBox> Body;
	EShopKind Kind = EShopKind::GeneralStore;
	ETab Tab = ETab::Sell;
	FIntPoint Selected = FIntPoint(1, 1);
};
