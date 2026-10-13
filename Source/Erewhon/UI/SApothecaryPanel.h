#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AApothecary;
class AErewhonPlayerController;
class SBox;
enum class EApothecaryStation : uint8;

/**
 * Le mortier ou le chaudron de l'apothicairerie : la liste des préparations possibles, avec
 * les ingrédients qu'on a sur soi, et la préparation en cours (un exemplaire à la fois).
 * Le chaudron ne chauffe que sur un feu allumé : on peut le nourrir d'ici.
 */
class SApothecaryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SApothecaryPanel) : _Controller(nullptr), _House(nullptr), _Station(static_cast<EApothecaryStation>(0)) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
		SLATE_ARGUMENT(AApothecary*, House)
		SLATE_ARGUMENT(EApothecaryStation, Station)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void Rebuild();
	TSharedRef<SWidget> BuildProgress() const;
	TSharedRef<SWidget> BuildFire() const;

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TWeakObjectPtr<AApothecary> House;
	bool bCauldron = false;
	TSharedPtr<SBox> Body;
};
