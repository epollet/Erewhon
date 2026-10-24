#include "Sky/CelestialSystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonSettings.h"
#include "World/ErewhonLights.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sky/ErewhonWind.h"
#include "World/ErewhonMaterials.h"
#include "Core/ErewhonUserSettings.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "World/ErewhonVisuals.h"

namespace
{
	/** Distance des astres dessinés (cm) : loin derrière tout le reste, devant rien. */
	constexpr float GSkyDistance = 800000.f;
	/** Diamètre apparent de la Lune (degrés) : plus grosse que la vraie, comme dans un conte. */
	constexpr float GMoonDegrees = 3.2f;
	constexpr int32 GStarCount = 900;
	/** Éblouissement, Soleil hors de l'axe du regard : bloom, ronds de l'objectif, halo rayonnant. */
	constexpr float GSunBloom = 0.9f;
	constexpr float GSunLensFlare = 0.55f;
	constexpr float GSunShaftBloom = 0.12f;
}

DEFINE_LOG_CATEGORY_STATIC(LogErewhonSky, Log, All);

// ── Orbites ──────────────────────────────────────────────────────────────────

float FOrbit::Angle(double WorldClock) const
{
	return FMath::Fmod(UE_DOUBLE_TWO_PI * WorldClock / PeriodSeconds + Phase, UE_DOUBLE_TWO_PI);
}

FVector FOrbit::Direction(double WorldClock) const
{
	const float A = Angle(WorldClock);
	const float Tilt = FMath::DegreesToRadians(TiltDegrees);
	// Plan est-ouest incliné vers le sud : à midi l'astre culmine au sud du zénith.
	return FVector(-FMath::Sin(A) * FMath::Sin(Tilt), FMath::Cos(A), FMath::Sin(A) * FMath::Cos(Tilt));
}

FString ACelestialSystem::ClockString(const FOrbit& Orbit, double WorldClock)
{
	const float Hours = FMath::Fmod(Orbit.Angle(WorldClock) / UE_TWO_PI + 0.25f, 1.f) * 24.f;
	const int32 H = FMath::FloorToInt32(Hours);
	const int32 M = FMath::FloorToInt32((Hours - H) * 60.f);
	return FString::Printf(TEXT("%02d:%02d"), H, M);
}

ACelestialSystem* ACelestialSystem::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<ACelestialSystem> It(World);
	return It ? *It : nullptr;
}

// ── Construction ─────────────────────────────────────────────────────────────

ACelestialSystem::ACelestialSystem()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);
	// Ciel un peu plus dense et bleu-vert : profondeur aérienne marquée sur l'horizon.
	Atmosphere->SetRayleighScatteringScale(0.045f);
	Atmosphere->SetMieScatteringScale(0.003f);
	Atmosphere->SetAerialPespectiveViewDistanceScale(2.f);
	// Sol de la planète vu au-delà de l'horizon : vert prairie plutôt que gris sombre.
	Atmosphere->SetGroundAlbedo(FColor(70, 110, 50));

	const auto MakeLight = [this](const TCHAR* Name, const FLinearColor& Color, float SourceAngle, int32 AtmosphereIndex)
	{
		UDirectionalLightComponent* Light = CreateDefaultSubobject<UDirectionalLightComponent>(Name);
		Light->SetupAttachment(Root);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetLightColor(Color);
		Light->LightSourceAngle = SourceAngle;   // disque large : ombres douces
		Light->bAtmosphereSunLight = AtmosphereIndex >= 0;
		Light->AtmosphereSunLightIndex = FMath::Max(0, AtmosphereIndex);
		Light->SetCastShadows(true);
		return Light;
	};
	Sun = MakeLight(TEXT("Sun"), FLinearColor::FromSRGBColor(FColor(255, 240, 210)), 2.0f, 0);
	Usha = MakeLight(TEXT("Usha"), FLinearColor::FromSRGBColor(FColor(255, 160, 110)), 4.5f, 1);
	Moon = MakeLight(TEXT("Moon"), FLinearColor::FromSRGBColor(FColor(190, 210, 255)), 1.5f, -1);
	Moon->SetCastShadows(false);
	// Les nuages font de l'ombre au sol, et le Soleil perce en rayons à travers eux.
	Sun->bCastCloudShadows = true;
	Sun->CloudShadowStrength = 0.65f;
	// L'ombre des nuages coûte cher (une carte d'ombre calculée par rayons dans les nuages) : on la limite
	// aux 20 km autour du joueur, à demi-résolution et avec moins d'échantillons (150 km par défaut).
	Sun->CloudShadowExtent = 20.f;
	Sun->CloudShadowMapResolutionScale = 0.5f;
	Sun->CloudShadowRaySampleCountScale = 0.35f;
	// Halo rayonnant autour du Soleil (qualité 2 et plus) : discret, et plafonné pour qu'un regard
	// en face ne noie pas un tiers de l'écran (UpdateSunGlare l'atténue encore dans l'axe).
	Sun->bEnableLightShaftBloom = true;
	Sun->BloomScale = GSunShaftBloom;
	Sun->BloomThreshold = 4.f;
	Sun->BloomMaxBrightness = 4.f;
	// Disque solaire un peu moins lumineux : il reste blanc, mais nourrit moins le bloom.
	Sun->AtmosphereSunDiskColorScale = FLinearColor(0.6f, 0.6f, 0.6f);
	// Une seule ombre portée, celle du Soleil : la seconde carte d'ombres d'Usha coûte cher.
	Usha->SetCastShadows(false);
	// Une seule lumière sert à la brume et aux translucides : le Soleil d'abord.
	Sun->ForwardShadingPriority = 2;
	Usha->ForwardShadingPriority = 1;
	Moon->ForwardShadingPriority = 0;

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	// Les ombres ne sont jamais noires : le ciel les emplit de bleu.
	SkyLight->SetIntensity(1.3f);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	// Brume légère et lointaine : elle adoucit la limite d'affichage de l'herbe.
	Fog->SetFogDensity(0.007f);
	Fog->SetFogHeightFalloff(0.15f);
	Fog->SetStartDistance(5000.f);

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);
	// Une couche de 4 km d'épaisseur : assez pour de gros cumulus, moins de nuage à traverser à chaque rayon.
	Clouds->SetLayerHeight(4.f);

	Grading = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grading"));
	Grading->SetupAttachment(Root);
	Grading->bUnbound = true;
	FPostProcessSettings& PP = Grading->Settings;
	// Étalonnage pastoral : saturation un peu poussée, contraste adouci, léger bloom.
	PP.bOverride_ColorSaturation = true;
	PP.ColorSaturation = FVector4(1.12f, 1.12f, 1.12f, 1.f);
	PP.bOverride_ColorContrast = true;
	PP.ColorContrast = FVector4(0.96f, 0.96f, 0.96f, 1.f);
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = GSunBloom;
	// Face au Soleil, des ronds lumineux alignés (reflets dans l'objectif). Disponibles à toutes les
	// qualités (r.LensFlareQuality=1 au minimum, Config/DefaultScalability.ini).
	PP.bOverride_LensFlareIntensity = true;
	PP.LensFlareIntensity = GSunLensFlare;
	PP.bOverride_LensFlareBokehSize = true;
	PP.LensFlareBokehSize = 2.5f;
	// Pas de flou de mouvement : une image nette, comme peinte (et des étoiles qui restent des points).
	PP.bOverride_MotionBlurAmount = true;
	PP.MotionBlurAmount = 0.f;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.25f;
	// Exposition adaptative bornée : nuits sombres mais lisibles.
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	PP.bOverride_AutoExposureMinBrightness = true;
	PP.AutoExposureMinBrightness = -1.5f;
	PP.bOverride_AutoExposureMaxBrightness = true;
	PP.AutoExposureMaxBrightness = 6.f;
	PP.bOverride_AutoExposureSpeedUp = true;
	PP.AutoExposureSpeedUp = 2.f;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = -0.5f;
	PP.bOverride_AutoExposureSpeedDown = true;
	PP.AutoExposureSpeedDown = 1.f;

	const UErewhonSettings& Settings = UErewhonSettings::Get();
	// Un monde neuf : Soleil à l'est, Usha sous l'horizon (repris de 3DSimEco).
	SunOrbit = { Settings.SunPeriodSeconds, 0.f, 25.f };
	UshaOrbit = { Settings.UshaPeriodSeconds, UE_PI * 1.5f, 40.f };
	MoonOrbit = { Settings.SunPeriodSeconds, UE_PI, 15.f };
}

void ACelestialSystem::BeginPlay()
{
	Super::BeginPlay();
	ErewhonWeather::ReadTestOverrides();
	SetupCloudMaterial();
	ApplyUserSettings();
	BuildNightSky();
	if (UErewhonUserSettings* User = UErewhonUserSettings::Get())
	{
		User->OnWorldDetailChanged.AddUObject(this, &ACelestialSystem::ApplyUserSettings);
	}
}

void ACelestialSystem::ApplyUserSettings()
{
	// Les nuages volumétriques sont l'un des postes les plus lourds : on règle leur échantillonnage.
	const UErewhonUserSettings* User = UErewhonUserSettings::Get();
	const int32 Quality = User ? User->CloudQuality : 1;
	const float Scale = Quality == 0 ? 0.25f : Quality == 1 ? 0.5f : 1.f;
	Clouds->SetViewSampleCountScale(Scale);
	Clouds->SetReflectionViewSampleCountScale(Scale);
	Clouds->SetShadowViewSampleCountScale(Scale);
	Clouds->SetShadowReflectionViewSampleCountScale(Scale);
}

void ACelestialSystem::SetupCloudMaterial()
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
	if (!Base)
	{
		UE_LOG(LogErewhonSky, Warning, TEXT("Matériau de nuages du moteur introuvable"));
		return;
	}
	CloudMaterial = UMaterialInstanceDynamic::Create(Base, this);
	Clouds->SetMaterial(CloudMaterial);

	// Les noms exacts varient selon la version du moteur : on les repère par mot-clé.
	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Ids;
	Base->GetAllScalarParameterInfo(Infos, Ids);
	for (const FMaterialParameterInfo& Info : Infos)
	{
		const FString Name = Info.Name.ToString();
		if (CloudCoverageParam.IsNone() && Name.Contains(TEXT("Coverage")))
		{
			CloudCoverageParam = Info.Name;
			Base->GetScalarParameterValue(Info, CloudCoverageDefault);
		}
		if (Name == TEXT("StormClouds"))
		{
			CloudStormParam = Info.Name;
		}
	}
	Base->GetAllVectorParameterInfo(Infos, Ids);
	FLinearColor Wind = FLinearColor::Transparent;
	for (const FMaterialParameterInfo& Info : Infos)
	{
		if (Info.Name.ToString() == TEXT("Cloud_AlbedoColor"))
		{
			CloudAlbedoParam = Info.Name;
		}
		// Le vent du matériau fait défiler la carte des nuages : direction (RG), vitesses (BA).
		// On garde la direction, on accélère le défilement.
		if (Info.Name.ToString().Contains(TEXT("WindControls")) && Base->GetVectorParameterValue(Info, Wind))
		{
			const float Factor = UErewhonSettings::Get().CloudSpeedFactor;
			CloudMaterial->SetVectorParameterValue(Info.Name, FLinearColor(Wind.R, Wind.G, Wind.B * Factor, Wind.A * Factor));
			break;
		}
	}
	UE_LOG(LogErewhonSky, Log, TEXT("Nuages : couverture='%s' (défaut %.3f), vent %s"), *CloudCoverageParam.ToString(), CloudCoverageDefault, *Wind.ToString());
}

// ── Mise à jour ──────────────────────────────────────────────────────────────

void ACelestialSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State)
	{
		return;
	}
	const double Clock = State->GetWorldClock();
	Weather = State->SampleWeather();
	// La pluie voile le paysage par degrés : une averse (ou une météo forcée) ne fait pas sauter la brume.
	RainFog = RainFog < 0.f ? Weather.Rain : FMath::FInterpTo(RainFog, Weather.Rain, DeltaSeconds, 0.25f);
	// Sous un ciel lourd, le Soleil n'éclaire plus qu'à travers les nuages.
	UpdateBody(Sun, SunOrbit, SunLux * FMath::Lerp(1.f, 0.42f, Weather.Darkness), Clock);
	UpdateBody(Usha, UshaOrbit, UshaLux, Clock);
	UpdateBody(Moon, MoonOrbit, MoonLux, Clock);
	UpdateClouds(Clock);
	UpdateNightSky(Clock);
	UpdateSunGlare(Clock);
	// Les lanternes du village s'allument quand le Soleil passe sous l'horizon.
	if (UErewhonLights* Lights = GetWorld()->GetSubsystem<UErewhonLights>())
	{
		Lights->SetNight(1.f - FMath::SmoothStep(-0.06f, 0.08f, static_cast<float>(SunOrbit.Direction(Clock).Z)));
	}
}

void ACelestialSystem::BuildNightSky()
{
	using namespace ErewhonVisuals;
	UMaterialInterface* SkyDisc = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_SkyDisc"));
	UStaticMesh* Quad = GetShape(EShape::Plane);
	if (!SkyDisc || !Quad)
	{
		UE_LOG(LogErewhonSky, Warning, TEXT("Lune et étoiles : M_SkyDisc introuvable (relancer import_generated.py)"));
		return;
	}

	// La Lune : un disque peint (T_moon), face à la caméra.
	MoonDisc = NewObject<UStaticMeshComponent>(this);
	MoonDisc->SetupAttachment(Root);
	MoonDisc->SetStaticMesh(Quad);
	MoonGlow = UMaterialInstanceDynamic::Create(SkyDisc, this);
	MoonGlow->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.95f, 0.95f, 1.f));
	MoonDisc->SetMaterial(0, MoonGlow);
	MoonDisc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoonDisc->SetCastShadow(false);
	MoonDisc->SetBoundsScale(1000.f);
	MoonDisc->RegisterComponent();

	// Les étoiles : des points doux (T_star) semés sur la voûte, qui tournent avec elle.
	StarDome = NewObject<USceneComponent>(this);
	StarDome->SetupAttachment(Root);
	StarDome->RegisterComponent();
	Stars = NewObject<UInstancedStaticMeshComponent>(this);
	Stars->SetupAttachment(StarDome);
	Stars->SetStaticMesh(Quad);
	StarGlow = UMaterialInstanceDynamic::Create(SkyDisc, this);
	if (UTexture* Star = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_star")))
	{
		StarGlow->SetTextureParameterValue(TEXT("Tex"), Star);
	}
	StarGlow->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.9f, 0.93f, 1.f));
	Stars->SetMaterial(0, StarGlow);
	Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stars->SetCastShadow(false);
	Stars->SetBoundsScale(1000.f);
	Stars->RegisterComponent();
	FRandomStream Rand(20261012);
	for (int32 i = 0; i < GStarCount; ++i)
	{
		// Tout le ciel (la voûte tourne : celles d'en dessous se lèveront) ; quelques-unes plus grosses.
		const FVector Dir = Rand.GetUnitVector();
		const float Degrees = Rand.FRand() < 0.06f ? Rand.FRandRange(0.45f, 0.6f) : Rand.FRandRange(0.2f, 0.34f);
		const float Size = 2.f * GSkyDistance * FMath::Tan(FMath::DegreesToRadians(Degrees * 0.5f)) / 100.f;
		Stars->AddInstance(FTransform(FRotationMatrix::MakeFromZ(-Dir).ToQuat(), Dir * GSkyDistance, FVector(Size)));
	}
	MoonDisc->SetVisibility(false);
	Stars->SetVisibility(false);
}

void ACelestialSystem::UpdateNightSky(double WorldClock)
{
	if (!MoonDisc || !Stars)
	{
		return;
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;

	// Part de nuit : le Soleil couché, et Usha aussi (son ciel orangé efface les étoiles).
	const float SunUp = FMath::SmoothStep(-0.16f, 0.04f, static_cast<float>(SunOrbit.Direction(WorldClock).Z));
	const float UshaUp = FMath::SmoothStep(-0.1f, 0.1f, static_cast<float>(UshaOrbit.Direction(WorldClock).Z));
	const float Night = (1.f - SunUp) * (1.f - 0.85f * UshaUp);

	// La Lune, en face du Soleil, bien plus pâle que lui.
	const FVector ToMoon = MoonOrbit.Direction(WorldClock);
	const float MoonUp = FMath::SmoothStep(-0.06f, 0.04f, static_cast<float>(ToMoon.Z));
	const float MoonLevel = MoonUp * FMath::Lerp(0.25f, 1.f, Night);
	MoonDisc->SetVisibility(MoonLevel > 0.01f);
	if (MoonLevel > 0.01f)
	{
		const float Size = 2.f * GSkyDistance * FMath::Tan(FMath::DegreesToRadians(GMoonDegrees * 0.5f)) / 100.f;
		MoonDisc->SetWorldLocationAndRotation(Eye + ToMoon * GSkyDistance, FRotationMatrix::MakeFromZ(-ToMoon).ToQuat());
		MoonDisc->SetWorldScale3D(FVector(Size));
		MoonGlow->SetScalarParameterValue(TEXT("Intensity"), 2.2f);
		MoonGlow->SetScalarParameterValue(TEXT("Fade"), MoonLevel);
	}

	// Les étoiles tournent avec la voûte, autour de l'axe de l'orbite du Soleil.
	Stars->SetVisibility(Night > 0.01f);
	if (Night > 0.01f)
	{
		const float Tilt = FMath::DegreesToRadians(SunOrbit.TiltDegrees);
		const FVector Axis(FMath::Cos(Tilt), 0.f, FMath::Sin(Tilt));
		// Par petits pas seulement (100 m de marche, moins d'un pixel de rotation) : déplacées à chaque image,
		// les instances laissent des traînées à l'anticrénelage temporel.
		const FQuat Turn(Axis, SunOrbit.Angle(WorldClock));
		if (FVector::Dist(StarDome->GetComponentLocation(), Eye) > 10000.f || StarDome->GetComponentQuat().AngularDistance(Turn) > FMath::DegreesToRadians(0.08f))
		{
			StarDome->SetWorldLocationAndRotation(Eye, Turn);
		}
		StarGlow->SetScalarParameterValue(TEXT("Intensity"), 3.f);
		StarGlow->SetScalarParameterValue(TEXT("Fade"), Night);
	}
}

void ACelestialSystem::UpdateBody(UDirectionalLightComponent* Light, const FOrbit& Orbit, float MaxIntensity, double WorldClock) const
{
	const FVector ToBody = Orbit.Direction(WorldClock);
	// La lumière voyage de l'astre vers le sol.
	Light->SetWorldRotation(FRotationMatrix::MakeFromX(-ToBody).Rotator());

	// Fondu autour de l'horizon ; sous l'horizon, l'astre n'éclaire plus.
	const float Fade = FMath::SmoothStep(-0.04f, 0.12f, static_cast<float>(ToBody.Z));
	Light->SetIntensity(MaxIntensity * Fade);
	const bool bVisible = Fade > 0.f;
	if (Light->IsVisible() != bVisible)
	{
		Light->SetVisibility(bVisible);
	}
}

void ACelestialSystem::UpdateSunGlare(double WorldClock)
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	// Regard dans l'axe du Soleil (à moins de ~18°) : l'éblouissement est adouci, pas supprimé.
	// Un peu à côté, bloom et ronds de l'objectif gardent toute leur force.
	const FVector ToSun = SunOrbit.Direction(WorldClock);
	const float Facing = static_cast<float>(FVector::DotProduct(PC->PlayerCameraManager->GetCameraRotation().Vector(), ToSun));
	const float Glare = FMath::SmoothStep(0.95f, 0.9986f, Facing) * FMath::SmoothStep(-0.04f, 0.12f, static_cast<float>(ToSun.Z));

	FPostProcessSettings& PP = Grading->Settings;
	PP.BloomIntensity = FMath::Lerp(GSunBloom, GSunBloom * 0.5f, Glare);
	PP.LensFlareIntensity = FMath::Lerp(GSunLensFlare, GSunLensFlare * 0.6f, Glare);
	// Changer le halo rayonnant recrée la lumière côté rendu : seulement par paliers.
	const float ShaftBloom = FMath::Lerp(GSunShaftBloom, GSunShaftBloom * 0.3f, Glare);
	if (FMath::Abs(Sun->BloomScale - ShaftBloom) > 0.004f)
	{
		Sun->SetBloomScale(ShaftBloom);
	}
}

void ACelestialSystem::UpdateClouds(double WorldClock)
{
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	if (UErewhonMaterials* Materials = GetWorld()->GetSubsystem<UErewhonMaterials>())
	{
		// L'herbe ondule avec le même vent que les nuages : plus fort, plus vite.
		// L'herbe ondule avec le vent du moment : la météo le renforce (orage) ou le calme.
		const float Strength = FMath::Clamp(static_cast<float>(ErewhonWind::Velocity(WorldClock).Size() / (ErewhonWind::BaseSpeed + ErewhonWind::SpeedAmplitude)) * Weather.Wind, 0.f, 2.5f);
		Materials->SetWind(FMath::Lerp(0.15f, 0.6f, FMath::Min(Strength, 1.f)) * FMath::Lerp(1.f, 1.8f, FMath::Clamp(Strength - 1.f, 0.f, 1.f)),
			FMath::Lerp(0.35f, 1.1f, FMath::Min(Strength, 1.f)) * FMath::Lerp(1.f, 1.6f, FMath::Clamp(Strength - 1.f, 0.f, 1.f)) * Settings.FoliageWindSpeedFactor);
	}
	if (CloudMaterial && !CloudCoverageParam.IsNone())
	{
		// La couverture, l'orage et la couleur des nuages suivent la météo.
		CloudMaterial->SetScalarParameterValue(CloudCoverageParam, Weather.Coverage);
		if (!CloudStormParam.IsNone())
		{
			CloudMaterial->SetScalarParameterValue(CloudStormParam, Weather.Storm);
		}
		if (!CloudAlbedoParam.IsNone())
		{
			const float Albedo = FMath::Lerp(0.98f, 0.62f, Weather.Darkness);
			CloudMaterial->SetVectorParameterValue(CloudAlbedoParam, FLinearColor(Albedo, Albedo, Albedo * 1.02f, 0.5f));
		}
	}
	// La couche de nuages, plus ou moins basse.
	if (FMath::Abs(Clouds->LayerBottomAltitude - Weather.CloudBase) > 0.02f)
	{
		Clouds->SetLayerBottomAltitude(Weather.CloudBase);
	}
	// La brume : la pluie l'épaissit, jusqu'au premier plan et assez haut pour noyer les montagnes au loin
	// (sans tout blanchir : à pleine averse, on voit encore à deux cents mètres) ; au soleil humide,
	// elle se couche dans les creux.
	Fog->SetFogDensity(0.007f + 0.045f * RainFog + 0.09f * Weather.Mist);
	Fog->SetFogHeightFalloff(FMath::Lerp(FMath::Lerp(0.15f, 0.1f, RainFog), 0.6f, Weather.Mist));
	Fog->SetStartDistance(FMath::Lerp(5000.f, 300.f, FMath::Max(Weather.Mist, RainFog * 0.85f)));
	SkyLight->SetIntensity(FMath::Lerp(1.3f, 1.05f, Weather.Darkness));
	// Un ciel chargé coûte cher à calculer : on échantillonne les nuages un peu moins finement.
	const UErewhonUserSettings* User = UErewhonUserSettings::Get();
	const int32 Quality = User ? User->CloudQuality : 1;
	const float Samples = (Quality == 0 ? 0.25f : Quality == 1 ? 0.5f : 1.f) * FMath::Lerp(1.f, 0.55f, FMath::SmoothStep(-0.2f, 0.5f, Weather.Coverage));
	if (FMath::Abs(Clouds->ViewSampleCountScale - Samples) > 0.02f)
	{
		Clouds->SetViewSampleCountScale(Samples);
		Clouds->SetShadowViewSampleCountScale(Samples);
	}
}
