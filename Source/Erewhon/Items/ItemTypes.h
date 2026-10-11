#pragma once

#include "CoreMinimal.h"
#include "World/SectorTypes.h"
#include "ItemTypes.generated.h"

UENUM(BlueprintType)
enum class EItemCategory : uint8
{
	Plant,     // récoltable dans les secteurs
	Tool,      // équipable dans un slot d'équipement (« Mains »)
	Clothing,  // équipable dans un slot de vêtement, peut ajouter des poches
	Resource,
};

UENUM(BlueprintType)
enum class EClothingSlot : uint8
{
	None,
	Torso,
	Legs,
};

/** Forme du placeholder procédural d'une plante, tant qu'aucun mesh n'est fourni. */
UENUM(BlueprintType)
enum class EPlantShape : uint8
{
	Grass,
	Flower,
	Herb,
};

/** Paramètres de génération d'une plante récoltable. */
USTRUCT(BlueprintType)
struct FPlantSpawnDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPlantShape Shape = EPlantShape::Grass;

	/** Nombre d'exemplaires générés par type de secteur. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<ESectorType, int32> CountPerSector;

	/** Hauteur de la plante (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HeightMin = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HeightMax = 0.6f;

	/** Couleur secondaire : cœur de la fleur, feuilles de l'herbe aromatique. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLinearColor AccentColor = FLinearColor::Yellow;

	/** Distance au-delà de laquelle l'instance n'est plus affichée (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CullDistanceMeters = 80.f;

	/** Exemplaires par point d'apparition (touffe) ; récoltés ensemble. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ClusterMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ClusterMax = 1;

	/** Rayon de la touffe (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ClusterRadiusMeters = 0.25f;

	/** Sel du générateur : changer la valeur redistribue les positions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 SeedSalt = 0;

	int32 CountFor(ESectorType Type) const
	{
		const int32* Count = CountPerSector.Find(Type);
		if (!Count && Type == ESectorType::Road)
		{
			Count = CountPerSector.Find(ESectorType::Prairie);   // bas-côtés de la route : de la prairie
		}
		return Count ? *Count : 0;
	}
};

/** Définition d'un objet, chargée depuis Content/Data/Items.json. */
USTRUCT(BlueprintType)
struct FItemDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EItemCategory Category = EItemCategory::Resource;

	/** Prix de vente au marchand (0 = non vendable, hors reprise d'occasion). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 SellPrice = 0;

	/** Prix d'achat au marchand (0 = non vendu par le marchand). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 BuyPrice = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MaxStack = 1;

	/** Couleur de repli pour les icônes et les placeholders 3D. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLinearColor Color = FLinearColor::White;

	/** Un outil marqué ainsi ne bloque pas la récolte des plantes (cerpe). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bHarvestTool = false;

	// ── Vêtements ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EClothingSlot ClothingSlot = EClothingSlot::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 PocketSlots = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ExtraEquipSlots = 0;

	/** Un short ne couvre que le haut des jambes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bShortLegs = false;

	// ── Plantes ──
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FPlantSpawnDef Plant;

	// ── Assets générés (Meshy / Gemini) ──
	// Laissés vides dans le JSON, ils sont déduits des conventions de nommage s'ils existent.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FSoftObjectPath Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FSoftObjectPath Icon;

	/** Variantes de mesh tirées au hasard par exemplaire (prioritaires sur Mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FSoftObjectPath> MeshVariants;

	/** Matériau imposé au mesh (ex. M_Foliage pour l'herbe procédurale). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FSoftObjectPath Material;

	bool IsValid() const { return !Id.IsNone(); }
	bool IsEquipable() const { return Category == EItemCategory::Tool; }
	bool IsClothing() const { return Category == EItemCategory::Clothing; }
	bool IsUnique() const { return Category == EItemCategory::Tool; }

	/** Prix payé par le marchand pour un exemplaire. */
	int32 ResalePrice(float ResaleRatio) const
	{
		if (SellPrice > 0)
		{
			return SellPrice;
		}
		return FMath::FloorToInt32(BuyPrice * ResaleRatio);
	}
};
