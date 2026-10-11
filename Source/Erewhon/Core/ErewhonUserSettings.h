#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "ErewhonUserSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnWorldDetailChanged);

/**
 * Réglages propres à chaque poste (Saved/Config/…/GameUserSettings.ini) : les options
 * graphiques du moteur plus celles du monde d'Erewhon (végétation, distance de vue).
 * Ces dernières n'influent que sur l'affichage : la liste des plantes récoltables
 * reste identique pour tous les joueurs.
 */
UCLASS()
class EREWHON_API UErewhonUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	static UErewhonUserSettings* Get();

	virtual void SetToDefaults() override;

	/** Secteurs affichés autour du joueur (1 → 3×3, 3 → 7×7). */
	UPROPERTY(Config)
	int32 ViewDistanceSectors = 2;

	/** Multiplicateur du nombre de plantes décoratives (fougères, buissons…). */
	UPROPERTY(Config)
	float VegetationDensity = 1.f;

	/** Multiplicateur de la distance d'affichage de toute la végétation. */
	UPROPERTY(Config)
	float VegetationDistance = 1.f;

	UPROPERTY(Config)
	bool bVegetationShadows = true;

	UPROPERTY(Config)
	bool bShowFps = true;

	/** Volumes (0 → 1) : bruitages (pas, porte, oiseaux) et ambiance (vent, herbe). */
	UPROPERTY(Config)
	float EffectsVolume = 0.8f;

	UPROPERTY(Config)
	float AmbientVolume = 0.6f;

	/** Volume de la musique d'ambiance (0 → 1). */
	UPROPERTY(Config)
	float MusicVolume = 0.5f;

	/** Échantillonnage des nuages volumétriques : 0 faible, 1 moyen, 2 haut. */
	UPROPERTY(Config)
	int32 CloudQuality = 1;

	/** À appeler après un changement : les secteurs se reconstruisent. */
	void NotifyWorldDetailChanged();

	FOnWorldDetailChanged OnWorldDetailChanged;
};
