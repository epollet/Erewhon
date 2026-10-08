#pragma once

#include "CoreMinimal.h"

/**
 * Vent déterministe, fonction de l'horloge du monde (repris de 3DSimEco) :
 * toutes les machines le calculent à l'identique, sans échange réseau.
 */
namespace ErewhonWind
{
	constexpr double BaseSpeed = 5.0;        // m/s
	constexpr double SpeedAmplitude = 3.0;   // m/s → vitesse dans [2, 8]
	constexpr double DirectionPeriod = 1800.0;
	constexpr double SpeedPeriod = 900.0;
	constexpr double BaseDirection = UE_DOUBLE_PI * 0.3;
	constexpr double DirectionAmplitude = 0.9;

	/** Vitesse instantanée (m/s) dans le plan XY. */
	inline FVector2D Velocity(double WorldClock)
	{
		const double Direction = BaseDirection + FMath::Sin(WorldClock * UE_DOUBLE_TWO_PI / DirectionPeriod) * DirectionAmplitude;
		const double Speed = BaseSpeed + SpeedAmplitude * FMath::Sin(WorldClock * UE_DOUBLE_TWO_PI / SpeedPeriod);
		return FVector2D(FMath::Cos(Direction), FMath::Sin(Direction)) * Speed;
	}

	/** Déplacement cumulé (m) : intégrale bornée de la vitesse, sans dérive. */
	inline FVector2D Offset(double WorldClock)
	{
		const double WD = UE_DOUBLE_TWO_PI / DirectionPeriod;
		const double WS = UE_DOUBLE_TWO_PI / SpeedPeriod;
		const double Distance = BaseSpeed * WorldClock + SpeedAmplitude / WS * (1.0 - FMath::Cos(WS * WorldClock));
		const double Lateral = DirectionAmplitude * BaseSpeed / WD * (1.0 - FMath::Cos(WD * WorldClock));
		const double CA = FMath::Cos(BaseDirection);
		const double SA = FMath::Sin(BaseDirection);
		return FVector2D(CA * Distance - SA * Lateral, SA * Distance + CA * Lateral);
	}
}
