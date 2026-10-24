#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "World/SectorTypes.h"

class AErewhonPlayerController;
class SBox;

/**
 * Comptoir d'un commerce de la place :
 * - General Store, en deux onglets : Acheter (outils et fournitures en cartes illustrées, seulement ce qui est proposé au joueur) et
 *   Vendre (les objets rangés, à l'unité ou en totalité) ;
 * - Sector Manager : la carte du pays (SSectorMap, qu'on fait glisser et zoome) ; on choisit une case, on l'achète ou la revend.
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
	/** Un secteur choisi sur la carte : seule sa fiche se refait, la carte garde son cadrage. */
	void OnMapSectorPicked(const FIntPoint& Sector);
	/** Le village de la mairie où se tient le joueur (celui dont la place est la plus proche). */
	FIntPoint MairieVillage() const;
	TSharedRef<SWidget> BuildBuy();
	TSharedRef<SWidget> BuildSell();
	TSharedRef<SWidget> BuildVillage();
	TSharedRef<SWidget> BuildClass();
	/** Seules les améliorations du village de cette mairie changent l'étal. */
	void OnVillageChanged(const FIntPoint& Changed) { if (Changed == MairieVillage()) { Rebuild(); } }

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TSharedPtr<SBox> Body;
	/** La liste qui défile (achats, ventes) : on garde sa position quand le panneau se refait. */
	TSharedPtr<class SScrollBox> ContentScroll;
	EShopKind Kind = EShopKind::GeneralStore;
	ETab Tab = ETab::Sell;
	ETab LastTab = ETab::Sell;
	FIntPoint Selected = FIntPoint(1, 1);
	/** La carte du maire, gardée d'une reconstruction à l'autre (cadrage, zoom), et la fiche du secteur choisi. */
	TSharedPtr<class SSectorMap> SectorMap;
	TSharedPtr<SBox> SectorDetail;
};
