#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/SectorTypes.h"
#include "SectorLayout.generated.h"

struct FItemDef;
class UItemDatabase;

/** Une plante récoltable, positionnée en coordonnées monde. */
struct FPlantInstance
{
	FName ItemId;
	FVector Location = FVector::ZeroVector;
	float Height = 0.f;   // m
	float Yaw = 0.f;      // degrés
};

/**
 * Contenu déterministe d'un secteur : type, plantes et zones exclues (chemins,
 * cabane). Identique sur le serveur et sur chaque client pour un même seed —
 * seul l'index d'une plante transite donc sur le réseau.
 */
struct EREWHON_API FSectorLayout
{
	FIntPoint Coord = FIntPoint::ZeroValue;
	ESectorType Type = ESectorType::Undefined;
	TArray<FPlantInstance> Plants;

	/** Rectangles monde sans végétation (chemins, parcelle de la cabane). */
	TArray<FBox2D> Exclusions;

	/** Chemins de terre à dessiner, sous-ensemble des exclusions. */
	TArray<FBox2D> Paths;

	/** Index des plantes par cellule de grille, pour le ciblage. */
	TArray<TArray<int32>> Cells;
	int32 CellsPerSide = 0;
	float CellSize = 0.f;

	/** Indices des plantes dont le centre est à moins de Radius de Point (plan XY). */
	void GatherNear(const FVector& Point, float Radius, TArray<int32>& Out) const;
};

/** Résultat d'un ciblage de plante. */
struct FPlantTarget
{
	FIntPoint Sector = FIntPoint::ZeroValue;
	int32 PlantIndex = INDEX_NONE;
	FName ItemId;
	FVector Location = FVector::ZeroVector;

	bool IsValid() const { return PlantIndex != INDEX_NONE; }
};

/**
 * Génère et met en cache les FSectorLayout. Utilisé par le rendu des secteurs
 * côté client et par la validation des récoltes côté serveur.
 */
UCLASS()
class EREWHON_API USectorLayoutSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	TSharedRef<const FSectorLayout> GetLayout(const FIntPoint& Sector, int32 WorldSeed);

	/**
	 * Plante visée depuis Eye dans la direction Look.
	 * ConeDegrees <= 0 : visée au rayon (plante la plus proche du réticule).
	 * ConeDegrees > 0 : la plus proche dans le cône horizontal (cerpe).
	 * Filter permet d'écarter les plantes récoltées ou les secteurs interdits.
	 */
	FPlantTarget FindTarget(const FVector& Eye, const FVector& Look, float Reach, float ConeDegrees, int32 WorldSeed,
		TFunctionRef<bool(const FIntPoint& Sector, int32 PlantIndex)> Filter);

private:
	TSharedRef<FSectorLayout> Build(const FIntPoint& Sector, int32 WorldSeed) const;

	TMap<FIntPoint, TSharedRef<FSectorLayout>> Cache;
	TArray<FIntPoint> CacheOrder;
	int32 CachedSeed = 0;
};
