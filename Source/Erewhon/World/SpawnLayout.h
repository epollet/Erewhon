#pragma once

#include "CoreMinimal.h"
#include "World/SectorTypes.h"

/**
 * Plan du secteur de spawn (0:0), partagé entre la génération de végétation
 * et la construction des cabanes : chemins en croix, place du General Store,
 * clairière du Sector Manager. Toutes les longueurs sont en cm.
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
	inline constexpr EShopKind ShopKinds[] = { EShopKind::GeneralStore, EShopKind::SectorManager, EShopKind::Tailor };

	/**
	 * Repère d'une cabane (monde) : le General Store au centre de la place, porte au sud ;
	 * le Sector Manager au bord de la route de l'est, le tailleur au bord de celle de l'ouest,
	 * portes tournées vers la route.
	 */
	EREWHON_API FTransform CabinTransform(EShopKind Kind);

	/** Nom affiché d'un commerce (enseignes, panneaux). */
	EREWHON_API FString ShopName(EShopKind Kind);

	/** Position du marchand d'un commerce, derrière son comptoir. */
	EREWHON_API FVector MerchantLocation(EShopKind Kind);

	/** Pied du perron, devant la porte : là où mènent les panneaux. */
	EREWHON_API FVector DoorstepLocation(EShopKind Kind);

	/** Surface sous les pieds, pour les bruits de pas. */
	enum class ESurface : uint8 { Grass, Dirt, Stone, Wood };

	/** Dans une cabane (sur son dallage). */
	EREWHON_API bool IsInsideCabin(const FVector& World);

	/** Sur le perron de bois devant une porte. */
	EREWHON_API bool IsOnPorch(const FVector& World);

	/** Gonds de la porte d'une cabane (bord −Y de l'ouverture, au ras du plancher), orientés comme elle. */
	EREWHON_API FTransform DoorHingeTransform(EShopKind Kind);

	/** Largeur d'un chemin d'accès vers un secteur acheté, et rayon de sa clairière. */
	constexpr float AccessPathWidth = 320.f;
	constexpr float AccessClearingRadius = 380.f;

	/** Chemins principaux du secteur (cœur, à fondre dans l'herbe), en coordonnées monde. */
	EREWHON_API void BuildPaths(const FIntPoint& Sector, ESectorType Type, TArray<FBox2D>& OutPaths);

	/** Point d'apparition des joueurs : sur le chemin sud, face à la porte du General Store. */
	EREWHON_API FTransform PlayerSpawnTransform();
}
