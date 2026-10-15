#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sky/ErewhonWeather.h"
#include "CelestialSystem.generated.h"

class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UVolumetricCloudComponent;

/** Un astre en orbite : période, phase et inclinaison de son plan vers le sud. */
USTRUCT(BlueprintType)
struct FOrbit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) float PeriodSeconds = 1200.f;
	/** Position angulaire à t = 0 (rad) : 0 = horizon est, π/2 = zénith. */
	UPROPERTY(EditAnywhere) float Phase = 0.f;
	/** Inclinaison du plan d'orbite vers le sud (degrés). */
	UPROPERTY(EditAnywhere) float TiltDegrees = 25.f;

	float Angle(double WorldClock) const;
	/** Direction unitaire vers l'astre. Est = +Y, sud = −X, zénith = +Z. */
	FVector Direction(double WorldClock) const;
};

/**
 * Ciel local : Soleil, Usha et Lune en orbite selon l'horloge du monde,
 * atmosphère, lumière du ciel, brume, nuages volumétriques et étalonnage.
 * Réglé pour une ambiance pastorale peinte : lumière chaude, ombres douces,
 * couleurs saturées, gros cumulus.
 */
UCLASS()
class EREWHON_API ACelestialSystem : public AActor
{
	GENERATED_BODY()

public:
	ACelestialSystem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Heure affichée pour un astre : 12:00 au zénith, 06:00 au lever. */
	static FString ClockString(const FOrbit& Orbit, double WorldClock);

	const FOrbit& GetSunOrbit() const { return SunOrbit; }
	const FOrbit& GetUshaOrbit() const { return UshaOrbit; }

	static ACelestialSystem* Find(const UObject* WorldContext);

	/** Le temps qu'il fait (calculé à chaque image, voir ErewhonWeather). */
	const ErewhonWeather::FState& GetWeather() const { return Weather; }

private:
	void UpdateBody(UDirectionalLightComponent* Light, const FOrbit& Orbit, float MaxIntensity, double WorldClock) const;
	void UpdateClouds(double WorldClock);
	void SetupCloudMaterial();
	void ApplyUserSettings();
	/** La Lune et les étoiles, des images lointaines qui suivent la caméra. */
	void BuildNightSky();
	void UpdateNightSky(double WorldClock);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> Usha;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> Moon;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPostProcessComponent> Grading;

	UPROPERTY(EditAnywhere, Category = "Sky") FOrbit SunOrbit;
	UPROPERTY(EditAnywhere, Category = "Sky") FOrbit UshaOrbit;
	UPROPERTY(EditAnywhere, Category = "Sky") FOrbit MoonOrbit;

	UPROPERTY(EditAnywhere, Category = "Sky") float SunLux = 10.f;
	UPROPERTY(EditAnywhere, Category = "Sky") float UshaLux = 2.f;
	UPROPERTY(EditAnywhere, Category = "Sky") float MoonLux = 0.35f;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> MoonDisc;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MoonGlow;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> StarDome;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Stars;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> StarGlow;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;
	FName CloudCoverageParam;
	float CloudCoverageDefault = 0.f;

	/** Amplitude relative de la variation de couverture autour de la valeur du matériau. */
	UPROPERTY(EditAnywhere, Category = "Sky") float CloudCoverageSwing = 0.5f;

	ErewhonWeather::FState Weather;
	/** Les noms des réglages du matériau de nuages (orage, couleur). */
	FName CloudStormParam;
	FName CloudAlbedoParam;
};
