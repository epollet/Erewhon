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

	/** Cabane : demi-longueur (axe X, nord-sud) et demi-largeur (axe Y). */
	constexpr float CabinHalfLength = 450.f;
	constexpr float CabinHalfWidth = 350.f;
	constexpr float CabinWallHeight = 300.f;
	constexpr float CabinWallThickness = 20.f;

	/** Plancher surélevé de la cabane. */
	constexpr float CabinFloorTop = 45.f;

	/** Porte, au milieu du mur sud (−X). */
	constexpr float DoorHalfWidth = 70.f;
	constexpr float DoorHeight = 220.f;

	/** Position des gonds de la porte (bord −Y de l'ouverture, au ras du plancher). */
	EREWHON_API FVector DoorHingeLocation();

	/** Chemins (cœur, à fondre dans l'herbe) en coordonnées monde. */
	EREWHON_API void BuildPaths(const FIntPoint& Sector, TArray<FBox2D>& OutPaths);

	/** Point d'apparition des joueurs : sur le chemin sud, face à la porte. */
	EREWHON_API FTransform PlayerSpawnTransform();
}
