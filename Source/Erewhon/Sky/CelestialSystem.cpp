#include "Sky/CelestialSystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonSettings.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sky/ErewhonWind.h"

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
	Atmosphere->SetRayleighScatteringScale(0.04f);
	Atmosphere->SetMieScatteringScale(0.006f);
	Atmosphere->SetAerialPespectiveViewDistanceScale(2.f);

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
	Fog->SetFogDensity(0.006f);
	Fog->SetFogHeightFalloff(0.15f);
	Fog->SetStartDistance(3000.f);

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);

	Grading = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grading"));
	Grading->SetupAttachment(Root);
	Grading->bUnbound = true;
	FPostProcessSettings& PP = Grading->Settings;
	// Étalonnage pastoral : saturation un peu poussée, contraste adouci, léger bloom.
	PP.bOverride_ColorSaturation = true;
	PP.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	PP.bOverride_ColorContrast = true;
	PP.ColorContrast = FVector4(0.96f, 0.96f, 0.96f, 1.f);
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.9f;
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
	SetupCloudMaterial();
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
	}
	Base->GetAllVectorParameterInfo(Infos, Ids);
	for (const FMaterialParameterInfo& Info : Infos)
	{
		const FString Name = Info.Name.ToString();
		// « Layout_* » déplace la carte de couverture, ce n'est pas le vent.
		if (CloudWindParam.IsNone() && Name.Contains(TEXT("Wind")) && !Name.StartsWith(TEXT("Layout")))
		{
			CloudWindParam = Info.Name;
		}
	}
	UE_LOG(LogErewhonSky, Log, TEXT("Nuages : couverture='%s' (défaut %.3f), vent='%s'"), *CloudCoverageParam.ToString(), CloudCoverageDefault, *CloudWindParam.ToString());
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
	UpdateBody(Sun, SunOrbit, SunLux, Clock);
	UpdateBody(Usha, UshaOrbit, UshaLux, Clock);
	UpdateBody(Moon, MoonOrbit, MoonLux, Clock);
	UpdateClouds(Clock);
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

void ACelestialSystem::UpdateClouds(double WorldClock)
{
	if (!CloudMaterial)
	{
		return;
	}
	const UErewhonSettings& Settings = UErewhonSettings::Get();
	if (!CloudCoverageParam.IsNone())
	{
		// Le ciel se couvre et se dégage sur CloudCoveragePeriodSeconds, autour du réglage du matériau.
		const float Wave = FMath::Sin(WorldClock * UE_DOUBLE_TWO_PI / Settings.CloudCoveragePeriodSeconds);
		CloudMaterial->SetScalarParameterValue(CloudCoverageParam, CloudCoverageDefault * (1.f + CloudCoverageSwing * Wave));
	}
	if (!CloudWindParam.IsNone())
	{
		const FVector2D Wind = ErewhonWind::Velocity(WorldClock);
		CloudMaterial->SetVectorParameterValue(CloudWindParam, FLinearColor(Wind.X, Wind.Y, 0.f, 0.1f));
	}
}
