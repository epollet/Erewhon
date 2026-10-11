#include "World/Terrain.h"

#include "Core/ErewhonSettings.h"
#include "World/SectorTypes.h"

namespace ErewhonTerrain
{
	namespace
	{
		/** Décalage du bruit propre à chaque monde. */
		FVector2D SeedOffset(int32 WorldSeed, int32 Octave)
		{
			FRandomStream Rand(HashCombine(GetTypeHash(WorldSeed), GetTypeHash(Octave)));
			return FVector2D(Rand.FRandRange(-500.f, 500.f), Rand.FRandRange(-500.f, 500.f));
		}
	}

	float Height(const FVector2D& World, int32 WorldSeed)
	{
		const UErewhonSettings& Settings = UErewhonSettings::Get();
		const float Amplitude = Settings.TerrainAmplitudeMeters * 100.f;
		if (Amplitude <= 0.f)
		{
			return 0.f;
		}

		// Trois octaves : de larges collines, des vallonnements, un léger moutonnement.
		float Noise = 0.f;
		Noise += 0.62f * FMath::PerlinNoise2D(World / 9000.f + SeedOffset(WorldSeed, 0));
		Noise += 0.28f * FMath::PerlinNoise2D(World / 3400.f + SeedOffset(WorldSeed, 1));
		Noise += 0.10f * FMath::PerlinNoise2D(World / 1200.f + SeedOffset(WorldSeed, 2));
		// Ramené à [0, 1] puis adouci vers le bas : des fonds larges, des sommets ronds.
		const float Hill = FMath::Pow(FMath::Clamp(Noise * 0.5f + 0.5f, 0.f, 1.f), 1.4f);

		// Autour de la cabane, le sol est plat ; il rejoint les collines en pente douce.
		const float FromSpawn = FVector2D::Distance(World, FVector2D(ErewhonSector::Center(FIntPoint::ZeroValue)));
		const float FlatRadius = Settings.SpawnFlatRadiusMeters * 100.f;
		const float Flatten = FMath::SmoothStep(FlatRadius, FlatRadius + 3000.f, FromSpawn);

		return Hill * Amplitude * Flatten;
	}

	FVector Normal(const FVector2D& World, int32 WorldSeed)
	{
		constexpr float Step = 50.f;
		const float HX = Height(World + FVector2D(Step, 0.f), WorldSeed) - Height(World - FVector2D(Step, 0.f), WorldSeed);
		const float HY = Height(World + FVector2D(0.f, Step), WorldSeed) - Height(World - FVector2D(0.f, Step), WorldSeed);
		return FVector(-HX, -HY, 2.f * Step).GetSafeNormal();
	}
}
