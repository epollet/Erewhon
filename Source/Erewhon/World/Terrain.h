#pragma once

#include "CoreMinimal.h"

/**
 * Relief du monde, secteur par secteur : chaque coin de secteur a son altitude, partagée par les
 * secteurs qui le touchent, si bien que deux voisins se raccordent toujours exactement sur leur bord.
 * D'un coin à l'autre, le sol monte ou descend en douceur ; à l'intérieur s'ajoute un relief propre au
 * type (presque plat en prairie et en lisière, modéré en forêt), nul sur les bords.
 * La place du marchand reste parfaitement plane et s'y raccorde en pente douce.
 * Déterministe : serveur et clients calculent les mêmes hauteurs.
 */
namespace ErewhonTerrain
{
	/** Hauteur du sol (cm) au point monde. */
	EREWHON_API float Height(const FVector2D& World, int32 WorldSeed);

	/** Normale du sol, par différences finies. */
	EREWHON_API FVector Normal(const FVector2D& World, int32 WorldSeed);
}
