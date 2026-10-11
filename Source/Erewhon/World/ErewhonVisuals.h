#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class USceneComponent;
class AActor;

/**
 * Placeholders visuels construits à partir des formes de base du moteur,
 * en attendant les assets générés (Meshy pour la 3D, Gemini pour les textures).
 * Les couleurs suivent la direction artistique « pastorale peinte » : tons
 * saturés mais doux, verts lumineux, bois chauds.
 */
namespace ErewhonVisuals
{
	enum class EShape : uint8 { Cube, Cylinder, Cone, Sphere, Plane };

	EREWHON_API UStaticMesh* GetShape(EShape Shape);

	/** Matériau unicolore (BasicShapeMaterial, paramètre « Color »). */
	EREWHON_API UMaterialInstanceDynamic* MakeColorMaterial(UObject* Outer, const FLinearColor& Color);

	/**
	 * Ajoute à Owner une forme colorée, attachée à Parent.
	 * Size est la taille finale en cm (les formes de base font 100 cm de côté).
	 * Pour Cube/Cylinder/Sphere, Location désigne le centre ; pour Cone, la base.
	 */
	EREWHON_API UStaticMeshComponent* AddShape(AActor* Owner, USceneComponent* Parent, EShape Shape,
		const FVector& Location, const FVector& Size, const FLinearColor& Color,
		const FRotator& Rotation = FRotator::ZeroRotator, bool bCollision = true);

	// ── Assets générés (voir Tools/Editor/import_generated.py) ──────────────────

	/** Asset généré par son nom court, ex. LoadGenerated<UStaticMesh>("Meshes", "SM_barrel"). Nul s'il n'existe pas. */
	template <typename T>
	T* LoadGenerated(const TCHAR* Folder, const FString& Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/Generated/%s/%s.%s"), Folder, *Name, *Name);
		return Cast<T>(FSoftObjectPath(Path).TryLoad());
	}

	/**
	 * Matériau peint : texture générée T_<TextureName> projetée sur les axes du monde
	 * (TileSize en cm), teintée. Retombe sur une couleur unie si la texture manque.
	 */
	EREWHON_API UMaterialInterface* MakePaintedMaterial(UObject* Outer, const TCHAR* TextureName, float TileSize,
		const FLinearColor& Fallback, const FLinearColor& Tint = FLinearColor::White);

	/**
	 * Transformation qui ramène un mesh à la hauteur voulue (cm), posé sur Z = 0 et centré en XY.
	 * Les modèles générés arrivent à des échelles arbitraires.
	 */
	EREWHON_API FTransform FitToHeight(const UStaticMesh* Mesh, float HeightCm);

	/**
	 * Ajoute un prisme : un polygone convexe du plan YZ (cm, sens trigonométrique vu depuis +X),
	 * extrudé de X0 à X1. Sert aux pignons de la cabane. Collision incluse.
	 */
	EREWHON_API UProceduralMeshComponent* AddPrism(AActor* Owner, USceneComponent* Parent,
		const TArray<FVector2D>& PolygonYZ, float X0, float X1, UMaterialInterface* Material);

	/**
	 * Ajoute un tronc de prisme : deux contours horizontaux (mêmes nombres de sommets, sens
	 * trigonométrique vu de dessus) reliés par des flancs, fermé par le contour du haut.
	 * Sert au remblai de la cabane. Collision incluse.
	 */
	EREWHON_API UProceduralMeshComponent* AddLoft(AActor* Owner, USceneComponent* Parent,
		const TArray<FVector2D>& Bottom, float BottomZ, const TArray<FVector2D>& Top, float TopZ, UMaterialInterface* Material);

	/**
	 * Comme FitToHeight, mais la plus grande dimension horizontale vaut LengthCm, alignée sur Y.
	 * Avec HeightCm > 0, la hauteur est imposée aussi (échelle non uniforme).
	 */
	EREWHON_API FTransform FitToLength(const UStaticMesh* Mesh, float LengthCm, float HeightCm = 0.f);

	/** Ajoute à Owner un mesh généré, mis à la hauteur voulue. */
	EREWHON_API UStaticMeshComponent* AddGeneratedMesh(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh,
		const FVector& Location, float HeightCm, float Yaw = 0.f, bool bCollision = true);

	/** Palette commune. */
	namespace Palette
	{
		inline const FLinearColor Meadow = FLinearColor::FromSRGBColor(FColor(104, 160, 62));
		inline const FLinearColor MeadowSpawn = FLinearColor::FromSRGBColor(FColor(110, 166, 66));
		inline const FLinearColor Dirt = FLinearColor::FromSRGBColor(FColor(150, 116, 82));
		inline const FLinearColor WoodLight = FLinearColor::FromSRGBColor(FColor(196, 150, 96));
		inline const FLinearColor WoodDark = FLinearColor::FromSRGBColor(FColor(122, 84, 54));
		inline const FLinearColor Roof = FLinearColor::FromSRGBColor(FColor(176, 74, 58));
		inline const FLinearColor Stone = FLinearColor::FromSRGBColor(FColor(168, 164, 150));
		inline const FLinearColor Skin = FLinearColor::FromSRGBColor(FColor(240, 204, 170));
		inline const FLinearColor DefaultShorts = FLinearColor::FromSRGBColor(FColor(122, 72, 31));
		inline const FLinearColor Stem = FLinearColor::FromSRGBColor(FColor(84, 146, 52));
	}
}
