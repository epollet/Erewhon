#pragma once

#include "CoreMinimal.h"

/**
 * Les villages et leurs routes, en nombre infini, déduits de la graine du monde : chaque machine
 * les calcule à l'identique, sans échange réseau.
 *
 * Le monde est découpé en cellules de CellSectors × CellSectors secteurs, un village par cellule.
 * Son centre (le secteur de sa place) s'écarte d'au plus CenterJitter secteurs du milieu de la
 * cellule : deux centres voisins sont ainsi à 11 – 19,5 secteurs l'un de l'autre (jamais moins
 * de 10, jamais plus de 20 du plus proche). Le village de départ est la cellule 0:0, centré en 0:0.
 *
 * Chaque village est relié par une route à ses quatre voisins (nord, sud, est, ouest) : un tracé
 * en Z d'un secteur de large, qui quitte la place droit devant, oblique à mi-chemin, puis rejoint
 * la place voisine en face. Chaque place a donc exactement quatre sorties, et deux routes ne se
 * croisent qu'aux places. Dans ses secteurs, loin des places, le chemin serpente (RoadCurve).
 *
 * Un village est désigné par sa cellule (FIntPoint), jamais par le secteur de son centre.
 */
namespace ErewhonVillages
{
	/** Côté d'une cellule de village, en secteurs. */
	constexpr int32 CellSectors = 15;

	/** Écart maximal du centre au milieu de sa cellule, sur chaque axe, en secteurs. */
	constexpr int32 CenterJitter = 2;

	/** Le village de départ : celui des nouveaux joueurs, centré en 0:0. */
	inline const FIntPoint StartVillage = FIntPoint::ZeroValue;

	/**
	 * La graine du monde, dont dépendent les centres et les noms. Posée dès qu'elle est connue :
	 * par le serveur au chargement du monde, par les clients à la réplication de l'état du jeu.
	 */
	EREWHON_API void SetWorldSeed(int32 Seed);
	EREWHON_API int32 GetWorldSeed();

	// ── Villages ──

	/** Le secteur de la place d'un village. */
	EREWHON_API FIntPoint CenterOf(const FIntPoint& Village);

	/** Vrai si ce secteur est la place d'un village ; OutVillage reçoit alors ce village. */
	EREWHON_API bool IsCenter(const FIntPoint& Sector, FIntPoint* OutVillage = nullptr);

	/** Le village dont la place est la plus proche de ce secteur (distance euclidienne entre secteurs). */
	EREWHON_API FIntPoint NearestVillage(const FIntPoint& Sector);

	/**
	 * Le village auquel appartient un secteur, une fois pour toutes : libre ou acheté, place ou route,
	 * il ne dépend que de la graine (le village dont la place est la plus proche). C'est sa mairie qui
	 * le vend et le reprend.
	 */
	inline FIntPoint VillageOf(const FIntPoint& Sector) { return NearestVillage(Sector); }

	/** Les villages dont la place tombe dans ce rectangle de secteurs (bornes incluses), pour la carte. */
	EREWHON_API void VillagesIn(const FIntPoint& MinSector, const FIntPoint& MaxSector, TArray<FIntPoint>& OutVillages);

	/** Nom du village, façon Ghibli, stable pour une graine donnée (ex. « Hanamori », « Kazeno-Oka »). */
	EREWHON_API FText VillageName(const FIntPoint& Village);

	/**
	 * Repère du village : origine au centre de sa place, X vers le nord comme le monde, tourné de
	 * quarts de tour pour varier les villages (aucun pour le village de départ). Les plans du
	 * village (ErewhonSpawn, ErewhonVillage, ErewhonVillagers) sont écrits dans ce repère.
	 */
	EREWHON_API FTransform VillageFrame(const FIntPoint& Village);

	// ── Routes ──

	/** Secteur traversé par une route (les places n'en sont pas). */
	EREWHON_API bool IsRoad(const FIntPoint& Sector);

	/**
	 * Côtés (bits dans l'ordre d'ErewhonSector::Sides : nord, sud, est, ouest) par lesquels la route
	 * continue vers un secteur de route ou une place. Une place a ses quatre bits ; 0 hors des routes.
	 */
	EREWHON_API uint8 RoadSides(const FIntPoint& Sector);

	/**
	 * Le village où mène la route en quittant ce secteur (route ou place) par ce côté, en la suivant
	 * jusqu'à la prochaine place. Faux si la route ne sort pas par là.
	 */
	EREWHON_API bool RoadDestination(const FIntPoint& Sector, int32 Side, FIntPoint& OutVillage);

	/** Les quatre villages voisins reliés par la route, dans l'ordre d'ErewhonSector::Sides. */
	EREWHON_API void Neighbours(const FIntPoint& Village, FIntPoint OutNeighbours[4]);

	// ── Tracé des routes ──

	/** Écart maximal du passage de la route au milieu d'un côté, en fraction du côté : loin des coins. */
	constexpr float RoadSway = 0.22f;

	/**
	 * Où la route franchit ce côté du secteur : décalage depuis le milieu du côté, en fraction du côté
	 * (vers l'est sur un côté nord ou sud, vers le nord sur un côté est ou ouest), entre −RoadSway et
	 * RoadSway. Le même vu des deux secteurs qui le partagent : une houle douce le long des routes, un
	 * peu de hasard propre à chaque côté. Nul au bord d'une place et au bout du premier secteur qui en
	 * sort : près des villages, la route reste droite.
	 */
	EREWHON_API float RoadCrossing(const FIntPoint& Sector, int32 Side);

	/**
	 * Le tracé de la route dans un secteur de route (pas une place) : une courbe de Bézier cubique, en
	 * fraction du secteur (X vers le nord, Y vers l'est, de 0 à 1), d'un côté où elle continue à l'autre.
	 * Elle franchit chaque côté perpendiculairement, en RoadCrossing : les morceaux se raccordent sans
	 * cassure, et les coudes deviennent des arcs. Faux hors des routes.
	 */
	EREWHON_API bool RoadCurve(const FIntPoint& Sector, FVector2D OutControls[4]);

	/** Un point d'une courbe de Bézier cubique (T de 0 à 1). */
	inline FVector2D RoadCurvePoint(const FVector2D Controls[4], double T)
	{
		const double U = 1.0 - T;
		return Controls[0] * (U * U * U) + Controls[1] * (3.0 * U * U * T) + Controls[2] * (3.0 * U * T * T) + Controls[3] * (T * T * T);
	}
}
