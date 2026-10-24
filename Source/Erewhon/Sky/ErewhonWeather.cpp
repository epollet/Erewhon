#include "Sky/ErewhonWeather.h"

#define LOCTEXT_NAMESPACE "ErewhonWeather"

namespace ErewhonWeather
{
	namespace
	{
		float GForcedCloudiness = -1.f;
		float GForcedHumidity = -1.f;

		/** Un bruit lent, entre 0 et 1, propre à la graine du monde. */
		float Drift(double WorldClock, int32 WorldSeed, double Period, float Salt)
		{
			const float X = static_cast<float>(FMath::Fmod(WorldClock / Period, 100000.0)) + (WorldSeed % 1000) * 0.137f + Salt;
			// Deux octaves : de grands changements, et de petites hésitations.
			const float N = 0.75f * FMath::PerlinNoise1D(X) + 0.25f * FMath::PerlinNoise1D(X * 3.1f + 17.f);
			return FMath::Clamp(0.5f + N * 0.95f, 0.f, 1.f);
		}

		/** Valeur d'un barème par paliers (le temps continu en abscisse), interpolée en douceur. */
		float Table(float V, const TArray<TPair<float, float>>& Points)
		{
			if (V <= Points[0].Key)
			{
				return Points[0].Value;
			}
			for (int32 i = 1; i < Points.Num(); ++i)
			{
				if (V <= Points[i].Key)
				{
					const float T = (V - Points[i - 1].Key) / FMath::Max(0.0001f, Points[i].Key - Points[i - 1].Key);
					return FMath::Lerp(Points[i - 1].Value, Points[i].Value, FMath::SmoothStep(0.f, 1.f, T));
				}
			}
			return Points.Last().Value;
		}
	}

	void ReadTestOverrides()
	{
#if !UE_BUILD_SHIPPING
		FParse::Value(FCommandLine::Get(), TEXT("ErewhonWeather="), GForcedCloudiness);
		FParse::Value(FCommandLine::Get(), TEXT("ErewhonHumidity="), GForcedHumidity);
#endif
	}

	FState Sample(double WorldClock, int32 WorldSeed, TOptional<EKind> Forced)
	{
		FState S;
		// Le temps change en une vingtaine de minutes ; l'orage, au sommet de la courbe, monte et retombe par degrés.
		S.Cloudiness = GForcedCloudiness >= 0.f ? GForcedCloudiness : FMath::Pow(Drift(WorldClock, WorldSeed, 2600.0, 0.f), 1.15f);
		S.Humidity = GForcedHumidity >= 0.f ? GForcedHumidity : Drift(WorldClock, WorldSeed, 3700.0, 41.f);
		if (Forced.IsSet())
		{
			// Le milieu de chaque palier du barème ci-dessous ; au soleil, l'humidité décide de la brume.
			static constexpr float Typical[] = { 0.07f, 0.07f, 0.22f, 0.4f, 0.6f, 0.76f, 0.92f };
			S.Cloudiness = Typical[static_cast<uint8>(Forced.GetValue())];
			if (Forced.GetValue() == EKind::MistySun)
			{
				S.Humidity = 0.8f;
			}
			else if (Forced.GetValue() == EKind::Clear)
			{
				S.Humidity = 0.35f;
			}
		}
		const float V = S.Cloudiness;
		S.Kind = V < 0.14f ? (S.Humidity > 0.62f ? EKind::MistySun : EKind::Clear)
			: V < 0.3f ? EKind::FewClouds : V < 0.5f ? EKind::Cloudy : V < 0.68f ? EKind::Overcast : V < 0.84f ? EKind::Dark : EKind::Storm;

		S.Coverage = Table(V, { { 0.08f, -1.f }, { 0.22f, -0.55f }, { 0.4f, -0.2f }, { 0.6f, 0.25f }, { 0.78f, 0.55f }, { 0.95f, 0.75f } });
		S.Darkness = Table(V, { { 0.45f, 0.f }, { 0.65f, 0.35f }, { 0.8f, 0.65f }, { 0.95f, 0.85f } });
		S.Storm = Table(V, { { 0.8f, 0.f }, { 0.92f, 1.f } });
		// Nuages plus ou moins bas : leur altitude dérive, et ils descendent quand le ciel se charge.
		const float Altitude = Drift(WorldClock, WorldSeed, 1900.0, 83.f);
		S.CloudBase = FMath::Lerp(2.6f, 1.5f, Altitude) * FMath::Lerp(1.f, 0.8f, S.Darkness);   // jamais sous 1,2 km : la caméra dans les nuages coûte trop cher
		// La pluie : possible dès que c'est très couvert, selon l'humidité ; forte quand c'est sombre ; torrentielle à l'orage.
		const float RainChance = Table(V, { { 0.55f, 0.f }, { 0.7f, 0.45f }, { 0.85f, 0.8f }, { 0.95f, 1.f } });
		const float Showers = Drift(WorldClock, WorldSeed, 420.0, 151.f);   // les averses vont et viennent
		S.Rain = FMath::Clamp(RainChance * FMath::SmoothStep(0.25f, 0.6f, S.Humidity * 0.6f + Showers * 0.6f) * 1.3f, 0.f, 1.f);
		// La brume basse : au soleil et à l'humidité, surtout le matin… ici : quand l'air est humide et le ciel clair.
		S.Mist = FMath::SmoothStep(0.55f, 0.85f, S.Humidity) * (1.f - FMath::SmoothStep(0.15f, 0.35f, V));
		// Le vent : il dérive en tout temps, souffle plus fort sous un ciel lourd, en rafales à l'orage.
		const float Gust = Drift(WorldClock, WorldSeed, 600.0, 223.f);
		S.Wind = FMath::Lerp(0.35f, 1.5f, Gust) * FMath::Lerp(1.f, 1.6f, S.Darkness) + 1.2f * S.Storm;
		S.LightningPerMinute = S.Storm * 7.f;
		return S;
	}

	FText KindName(EKind Kind)
	{
		switch (Kind)
		{
		case EKind::MistySun:  return LOCTEXT("MistySun", "Soleil et brume");
		case EKind::Clear:     return LOCTEXT("Clear", "Grand soleil");
		case EKind::FewClouds: return LOCTEXT("FewClouds", "Quelques nuages");
		case EKind::Cloudy:    return LOCTEXT("Cloudy", "Nuageux");
		case EKind::Overcast:  return LOCTEXT("Overcast", "Très couvert");
		case EKind::Dark:      return LOCTEXT("Dark", "Ciel sombre");
		default:               return LOCTEXT("Storm", "Orage");
		}
	}

	float ShelterTarget(const FState& State)
	{
		return State.Kind == EKind::Storm ? 1.f : FMath::SmoothStep(0.55f, 0.85f, State.Rain);
	}
}

#undef LOCTEXT_NAMESPACE
