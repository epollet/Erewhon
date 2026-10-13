#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ErewhonSettings.generated.h"

/**
 * Toutes les constantes de jeu vivent ici (équivalent de la classe Config de 3DSimEco).
 * Éditables dans Project Settings > Game > Erewhon, persistées dans Config/DefaultGame.ini.
 * Les distances sont en mètres : la conversion en unités Unreal (cm) se fait via les accesseurs.
 */
/** Une espèce de végétation décorative, semée par bosquets. */
USTRUCT()
struct FDecorPlant
{
	GENERATED_BODY()

	/** Asset généré, relatif à /Game/Generated (ex. "Foliage/SM_grass_tall_a"). */
	UPROPERTY(EditAnywhere)
	FString Mesh;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0))
	int32 CountPrairie = 0;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 0))
	int32 CountSpawn = 0;

	/** Hauteur (m). */
	UPROPERTY(EditAnywhere)
	float HeightMin = 0.5f;

	UPROPERTY(EditAnywhere)
	float HeightMax = 1.f;

	/** Rayon d'un bosquet (m) ; 0 = semis uniforme. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = 0))
	float PatchRadius = 0.f;

	/** Exemplaires par bosquet. */
	UPROPERTY(EditAnywhere, meta = (ClampMin = 1))
	int32 PatchSize = 1;

	UPROPERTY(EditAnywhere, meta = (ClampMin = 10))
	float CullMeters = 70.f;

	UPROPERTY(EditAnywhere)
	bool bCastShadows = true;

	/** Matériau d'herbe ondulant au vent plutôt que celui du mesh. */
	UPROPERTY(EditAnywhere)
	bool bFoliageMaterial = false;

	/**
	 * Buisson : placé par le serveur comme par les clients (déterministe, indépendant du
	 * réglage de densité) parce qu'il ralentit le joueur.
	 */
	UPROPERTY(EditAnywhere)
	bool bSolid = false;

	/** Rayon d'emprise au sol d'un buisson, en fraction de sa hauteur. */
	UPROPERTY(EditAnywhere, meta = (EditCondition = "bSolid"))
	float FootprintRatio = 0.4f;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Erewhon"))
class EREWHON_API UErewhonSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UErewhonSettings();

	static const UErewhonSettings& Get() { return *GetDefault<UErewhonSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ── Monde ────────────────────────────────────────────────────────────────

	/** Côté d'un secteur carré (m). */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = 10))
	float SectorSizeMeters = 100.f;

	/** Nombre de secteurs chargés autour du joueur (2 → 5×5 secteurs). */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = 0, ClampMax = 6))
	int32 SectorRenderDistance = 2;

	/** Nom de la sauvegarde du monde côté serveur. */
	UPROPERTY(Config, EditAnywhere, Category = "World")
	FString WorldSaveSlot = TEXT("World_default");

	/** Intervalle entre deux sauvegardes automatiques (s). */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = 10))
	float AutosaveIntervalSeconds = 120.f;

	// ── Ciel ─────────────────────────────────────────────────────────────────

	/** Durée d'une rotation complète du Soleil (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = 10))
	float SunPeriodSeconds = 1200.f;

	/** Durée d'une rotation complète d'Usha (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = 10))
	float UshaPeriodSeconds = 3000.f;

	/** Période d'oscillation de la couverture nuageuse (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = 10))
	float CloudCoveragePeriodSeconds = 1200.f;

	/** Vitesse de défilement des nuages, relative à celle du matériau du moteur. */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = 0))
	float CloudSpeedFactor = 2.f;

	/** Vitesse d'ondulation de la végétation au vent (1 = réglage d'origine). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = 0))
	float FoliageWindSpeedFactor = 0.67f;

	// ── Prairie décorative ──

	/** Végétation non récoltable, purement visuelle : herbes, fougères, feuillages, buissons. */
	UPROPERTY(Config, EditAnywhere, Category = "Meadow", meta = (TitleProperty = "Mesh"))
	TArray<FDecorPlant> DecorPlants;

	/** Hauteur maximale des collines (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Meadow", meta = (ClampMin = 0))
	float TerrainAmplitudeMeters = 5.f;

	/** Rayon de la zone parfaitement plane autour de la cabane (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Meadow", meta = (ClampMin = 0))
	float SpawnFlatRadiusMeters = 16.f;

	/** Rayon moyen de la place de terre autour de la cabane (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Meadow", meta = (ClampMin = 0))
	float PlazaRadiusMeters = 9.5f;

	/** Largeur du fondu entre les chemins (ou la parcelle) et la prairie (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Meadow", meta = (ClampMin = 0))
	float PathBlendMeters = 1.5f;

	/** Les pavés accélèrent la marche. */
	UPROPERTY(Config, EditAnywhere, Category = "Player", meta = (ClampMin = 1))
	float PavedSpeedFactor = 1.2f;

	/** Prix d'un chemin d'accès pavé (après le pavage du village). */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = 0))
	int32 PavedLinkPrice = 50;

	/** Prix d'une amélioration « chemin jusqu'à la route » (pièces). */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = 0))
	int32 RoadLinkPrice = 50;

	/** Prix de l'apothicairerie, construite au centre d'une prairie à soi. */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = 0))
	int32 ApothecaryPrice = 2000;

	/** Prix de la remise de l'apothicairerie. */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = 0))
	int32 StoreroomPrice = 1000;

	// ── Récolte ──────────────────────────────────────────────────────────────

	/** Délai de repousse d'une plante récoltée (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Harvest", meta = (ClampMin = 1))
	float RegrowSeconds = 300.f;

	/** Distance maximale de ciblage d'une plante (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Harvest", meta = (ClampMin = 1))
	float HarvestReachMeters = 5.f;

	/** Ouverture du cône de ciblage de la cerpe (degrés, angle total). */
	UPROPERTY(Config, EditAnywhere, Category = "Harvest", meta = (ClampMin = 1, ClampMax = 180))
	float SickleConeDegrees = 75.f;

	// ── Économie ─────────────────────────────────────────────────────────────

	UPROPERTY(Config, EditAnywhere, Category = "Economy")
	int32 StartingCoins = 100;

	UPROPERTY(Config, EditAnywhere, Category = "Economy")
	int32 SectorBuyPrice = 100;

	UPROPERTY(Config, EditAnywhere, Category = "Economy")
	int32 SectorSellPrice = 50;

	/** Les outils et vêtements sont repris à cette fraction du prix neuf. */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = 0, ClampMax = 1))
	float ResaleRatio = 0.5f;

	/** Rayon (en secteurs, autour du spawn) des secteurs proposés à l'achat. */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = 1))
	int32 StoreSectorRadius = 4;

	/** Distance maximale pour dialoguer avec le marchand (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Economy", meta = (ClampMin = 1))
	float MerchantReachMeters = 5.f;

	// ── Inventaire ───────────────────────────────────────────────────────────

	UPROPERTY(Config, EditAnywhere, Category = "Inventory", meta = (ClampMin = 1))
	int32 BaseInventorySlots = 9;

	/** Slots d'équipement de base — le premier est « Mains ». */
	UPROPERTY(Config, EditAnywhere, Category = "Inventory", meta = (ClampMin = 1))
	int32 BaseEquipmentSlots = 1;

	// ── Joueur ───────────────────────────────────────────────────────────────

	UPROPERTY(Config, EditAnywhere, Category = "Player")
	float WalkSpeedMetersPerSecond = 4.f;

	UPROPERTY(Config, EditAnywhere, Category = "Player")
	float RunSpeedMetersPerSecond = 7.f;

	/** Ralentissement dans les hautes herbes et dans les buissons (multiplicateurs). */
	UPROPERTY(Config, EditAnywhere, Category = "Player", meta = (ClampMin = 0.1, ClampMax = 1))
	float GrassSpeedFactor = 0.75f;

	UPROPERTY(Config, EditAnywhere, Category = "Player", meta = (ClampMin = 0.1, ClampMax = 1))
	float BushSpeedFactor = 0.5f;

	// ── Accesseurs en unités Unreal (cm) ─────────────────────────────────────

	float SectorSize() const { return SectorSizeMeters * 100.f; }
	float HarvestReach() const { return HarvestReachMeters * 100.f; }
	float MerchantReach() const { return MerchantReachMeters * 100.f; }
};
