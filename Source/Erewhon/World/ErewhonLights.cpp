#include "World/ErewhonLights.h"

#include "Components/LightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** Les lanternes éclairent un peu moins que leur réglage d'origine. */
	constexpr float GBrightness = 0.8f;
}

void UErewhonLights::RegisterLight(ULightComponent* Light, float MaxIntensity, float Phase)
{
	if (Light)
	{
		// Chaque flamme a sa phase (partagée par la lumière et le verre d'une même lanterne).
		Lights.Add({ Light, MaxIntensity, Phase >= 0.f ? Phase : FMath::FRandRange(0.f, 1000.f), true });
	}
}

void UErewhonLights::RegisterGlow(UMaterialInstanceDynamic* Glow, float MaxIntensity, float Phase)
{
	if (Glow)
	{
		Glows.Add({ Glow, MaxIntensity, Phase >= 0.f ? Phase : FMath::FRandRange(0.f, 1000.f) });
	}
}

float UErewhonLights::Flicker(float Phase) const
{
	// Deux octaves de bruit : la flamme ondule lentement et frémit vite, parfois un petit sursaut.
	const float Slow = FMath::PerlinNoise1D(Time * 1.6f + Phase);
	const float Fast = FMath::PerlinNoise1D(Time * 9.f + Phase * 1.7f);
	return FMath::Clamp(1.f + 0.12f * Slow + 0.07f * Fast, 0.75f, 1.25f);
}

void UErewhonLights::Tick(float DeltaTime)
{
	Time += DeltaTime;
	Lights.RemoveAll([](const FLight& L) { return !L.Light.IsValid(); });
	Glows.RemoveAll([](const FGlow& G) { return !G.Glow.IsValid(); });

	const bool bLit = Night > 0.01f;
	for (FLight& L : Lights)
	{
		if (L.bVisible != bLit)
		{
			L.bVisible = bLit;
			L.Light->SetVisibility(bLit);
		}
		if (bLit)
		{
			L.Light->SetIntensity(L.MaxIntensity * GBrightness * Night * Flicker(L.Phase));
		}
	}
	for (const FGlow& G : Glows)
	{
		// Un peu de lueur même le jour : le verre reste chaud, il ne s'éteint pas tout à fait.
		const float Level = FMath::Lerp(0.15f, 1.f, Night) * (bLit ? Flicker(G.Phase) : 1.f);
		G.Glow->SetScalarParameterValue(TEXT("Intensity"), G.MaxIntensity * GBrightness * Level);
	}
}
