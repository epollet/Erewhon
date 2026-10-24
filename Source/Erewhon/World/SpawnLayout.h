#pragma once

#include "CoreMinimal.h"
#include "World/SectorTypes.h"

struct FPathCurve;

/**
 * Plan d'un village (le même pour tous, posé dans le repère de chacun : voir ErewhonVillages::VillageFrame),
 * partagé entre la génération de végétation et la construction des cabanes : chemins en croix, place du
 * village, clairières du Sector Manager et du General Store. Toutes les longueurs sont en cm.
 */
namespace ErewhonSpawn
{
	/** Largeur des chemins de terre. */
	constexpr float PathWidth = 500.f;

	/** Cabane, dans son repère : demi-longueur (axe X, porte côté −X) et demi-largeur (axe Y). */
	constexpr float CabinHalfLength = 450.f;
	constexpr float CabinHalfWidth = 350.f;
	constexpr float CabinWallHeight = 300.f;
	constexpr float CabinWallThickness = 20.f;

	/** Plancher surélevé de la cabane. */
	constexpr float CabinFloorTop = 45.f;

	/** Porte, au milieu du mur −X. */
	constexpr float DoorHalfWidth = 70.f;
	constexpr float DoorHeight = 220.f;

	/** Clairière de terre autour des cabanes au bord des routes. */
	constexpr float ManagerClearingRadius = 900.f;

	/** Les commerces de la place, dans l'ordre de construction. */
	inline constexpr EShopKind ShopKinds[] = { EShopKind::GeneralStore, EShopKind::SectorManager, EShopKind::Tailor, EShopKind::Breeder, EShopKind::Workshop };

	/** Demi-longueur (X) et demi-largeur (Y) d'une maison : l'éleveur et l'atelier sont plus grands. */
	EREWHON_API FVector2D CabinHalfSize(EShopKind Kind);
	EREWHON_API float CabinWallHeightFor(EShopKind Kind);

	/**
	 * L'annexe d'une maison, dans son repère : la grange de l'éleveur (ouverte vers −X, trois box),
	 * la cour de l'atelier (où sont exposés charrette et outils). Centre et demi-taille.
	 */
	EREWHON_API FVector2D AnnexCenter(EShopKind Kind);
	EREWHON_API FVector2D AnnexHalfSize(EShopKind Kind);
	EREWHON_API bool HasAnnex(EShopKind Kind);

	/**
	 * Le village d'un point du monde : celui dont la place est la plus proche (ils sont à plus de dix
	 * secteurs les uns des autres). C'est le sien qui compte pour un commerce, une mairie, un banc.
	 */
	EREWHON_API FIntPoint VillageAt(const FVector& World);

	/** Un rectangle du plan du village (dans son repère), en monde : les quarts de tour gardent les axes. */
	EREWHON_API FBox2D VillageBox(const FIntPoint& Village, const FBox2D& Local);

	/**
	 * Repère d'une cabane d'un village (monde) : le Sector Manager puis le General Store au bord de la
	 * route de l'est (de son repère), le tailleur au bord de celle de l'ouest, portes tournées vers la
	 * route ; la place reste libre.
	 */
	EREWHON_API FTransform CabinTransform(const FIntPoint& Village, EShopKind Kind);

	/** Nom affiché d'un commerce (enseignes, panneaux). */
	EREWHON_API FString ShopName(EShopKind Kind);

	/** Position du marchand d'un commerce, derrière son comptoir. */
	EREWHON_API FVector MerchantLocation(const FIntPoint& Village, EShopKind Kind);

	/** Pied du perron, devant la porte : là où mènent les panneaux. */
	EREWHON_API FVector DoorstepLocation(const FIntPoint& Village, EShopKind Kind);

	/** Surface sous les pieds, pour les bruits de pas. */
	enum class ESurface : uint8 { Grass, Dirt, Stone, Wood };

	/** Dans une cabane du village le plus proche (sur son dallage). */
	EREWHON_API bool IsInsideCabin(const FVector& World);

	/** Sur le perron de bois devant une porte (du village le plus proche). */
	EREWHON_API bool IsOnPorch(const FVector& World);

	/** Gonds de la porte d'une cabane (bord −Y de l'ouverture, au ras du plancher), orientés comme elle. */
	EREWHON_API FTransform DoorHingeTransform(const FIntPoint& Village, EShopKind Kind);

	/** Largeur d'un chemin d'accès vers un secteur acheté, et rayon de sa clairière. */
	constexpr float AccessPathWidth = 320.f;
	constexpr float AccessClearingRadius = 380.f;

	/**
	 * Chemins principaux du secteur (cœur, à fondre dans l'herbe), en coordonnées monde : la croix d'une
	 * place en rectangles, la route qui serpente en courbe.
	 */
	EREWHON_API void BuildPaths(const FIntPoint& Sector, ESectorType Type, TArray<FBox2D>& OutPaths, TArray<FPathCurve>& OutCurves);

	/** L'axe de la route dans un secteur de route (voir ErewhonVillages::RoadCurve), en monde, d'un côté à l'autre. Faux ailleurs. */
	EREWHON_API bool RoadCenterline(const FIntPoint& Sector, TArray<FVector2D>& OutPoints);

	/** Le point d'une ligne brisée le plus proche de World ; OutTangent : la direction de la ligne là (dans le sens des points). */
	EREWHON_API FVector2D ClosestOnLine(const TArray<FVector2D>& Points, const FVector2D& World, FVector2D* OutTangent = nullptr);

	/** Point d'apparition des joueurs : au village de départ, sur le chemin sud, face à la place. */
	EREWHON_API FTransform PlayerSpawnTransform();
}
