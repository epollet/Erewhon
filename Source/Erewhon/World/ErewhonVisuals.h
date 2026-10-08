#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
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
