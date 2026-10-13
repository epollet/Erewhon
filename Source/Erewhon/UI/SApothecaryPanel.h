#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AApothecary;
class AStorageActor;
class AErewhonPlayerController;
class SBox;

/**
 * Le mortier ou le chaudron de l'apothicairerie : la liste des préparations possibles, avec
 * les ingrédients qu'on a sur soi, et la préparation en cours (un exemplaire à la fois).
 * Le chaudron ne chauffe que sur un feu allumé : on peut le nourrir d'ici.
 */
class SApothecaryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SApothecaryPanel) : _Controller(nullptr), _Station(nullptr), _Craft(0) {}
		SLATE_ARGUMENT(AErewhonPlayerController*, Controller)
		/** Le mortier ou le chaudron (meuble posé). */
		SLATE_ARGUMENT(AStorageActor*, Station)
		/** Le poste (ErewhonApothecary::ECraft) : mortier, chaudron ou atelier. */
		SLATE_ARGUMENT(uint8, Craft)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void Rebuild();
	TSharedRef<SWidget> BuildProgress() const;
	TSharedRef<SWidget> BuildFire() const;

	TWeakObjectPtr<AErewhonPlayerController> Controller;
	TWeakObjectPtr<AStorageActor> Station;
	/** Pour le chaudron : la maison dont il pend à la crémaillère (nul s'il n'y pend pas). */
	TWeakObjectPtr<AApothecary> House;
	bool bCauldron = false;
	uint8 Craft = 0;
	TSharedPtr<SBox> Body;
};
