#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Écran de chargement plein écran, façon page de carnet : nom du jeu, barre d'avancement
 * et ligne d'état. Piloté par UErewhonLoadingScreen ; ne prend ni souris ni clavier.
 */
class SLoadingOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLoadingOverlay) {}
		SLATE_ATTRIBUTE(FText, Status)
		/** 0 à 1. */
		SLATE_ATTRIBUTE(float, Progress)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<float> Progress;
};
