#pragma once

#include "CoreMinimal.h"

/**
 * Plan du secteur de spawn (0:0), partagé entre la génération de végétation
 * et la construction de la cabane : chemins en croix et parcelle dégagée.
 * Toutes les longueurs sont en cm.
 */
namespace ErewhonSpawn
{
	/** Largeur des chemins de terre. */
	constexpr float PathWidth = 500.f;

	/** Demi-côté de la parcelle dégagée autour de la cabane. */
	constexpr float PlotHalfSize = 1100.f;

	/** Cabane : demi-longueur (axe X, nord-sud) et demi-largeur (axe Y). */
	constexpr float CabinHalfLength = 450.f;
	constexpr float CabinHalfWidth = 350.f;
	constexpr float CabinWallHeight = 300.f;

	/** Chemins (à dessiner) et zones exclues de la végétation, en coordonnées monde. */
	EREWHON_API void BuildExclusions(const FIntPoint& Sector, TArray<FBox2D>& OutPaths, TArray<FBox2D>& OutExclusions);

	/** Point d'apparition des joueurs : sur le chemin sud, face à la porte. */
	EREWHON_API FTransform PlayerSpawnTransform();
}
