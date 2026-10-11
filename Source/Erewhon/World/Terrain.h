#pragma once

#include "CoreMinimal.h"

/**
 * Relief du monde : une seule fonction de hauteur, continue et lisse sur tout le plan,
 * si bien que deux secteurs voisins se raccordent toujours exactement et sans cassure.
 * Collines douces (jusqu’à 5 m) au-dessus de Z = 0 (jamais de creux sous la prairie lointaine) ;
 * la place du marchand reste parfaitement plane et s'y raccorde en pente douce.
 * Déterministe : serveur et clients calculent les mêmes hauteurs.
 */
namespace ErewhonTerrain
{
	/** Hauteur du sol (cm) au point monde. */
	EREWHON_API float Height(const FVector2D& World, int32 WorldSeed);

	/** Normale du sol, par différences finies. */
	EREWHON_API FVector Normal(const FVector2D& World, int32 WorldSeed);
}
