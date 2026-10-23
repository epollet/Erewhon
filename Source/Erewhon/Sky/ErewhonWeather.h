#pragma once

#include "CoreMinimal.h"

/**
 * La météo : déterministe, fonction de l'horloge du monde et de sa graine. Chaque machine la calcule
 * à l'identique, sans échange réseau. Un « temps » continu (de 0, grand soleil, à 1, orage) dérive
 * lentement : on passe du soleil aux nuages, à la pluie, à l'orage, et l'on en revient par degrés.
 * L'humidité (brume au soleil), l'altitude des nuages et la force du vent dérivent de leur côté.
 */
namespace ErewhonWeather
{
	enum class EKind : uint8
	{
		MistySun,   // grand soleil, brume dans les creux
		Clear,      // grand soleil, pas un nuage
		FewClouds,  // quelques nuages
		Cloudy,     // nuageux
		Overcast,   // très couvert : pluie possible
		Dark,       // très sombre : pluie, voire forte
		Storm,      // orage : éclairs, tonnerre, vent, averses
	};

	struct FState
	{
		EKind Kind = EKind::FewClouds;
		/** Le temps continu (0 : grand soleil → 1 : orage), et l'humidité (0 → 1). */
		float Cloudiness = 0.3f;
		float Humidity = 0.3f;
		/** Matériau des nuages : couverture (Cloud_GlobalCoverage), part d'orage (StormClouds), noirceur (0 → 1). */
		float Coverage = -0.2f;
		float Storm = 0.f;
		float Darkness = 0.f;
		/** Base de la couche de nuages (km). */
		float CloudBase = 1.5f;
		/** Pluie (0 → 1), brume basse (0 → 1), multiplicateur du vent, éclairs par minute. */
		float Rain = 0.f;
		float Mist = 0.f;
		float Wind = 1.f;
		float LightningPerMinute = 0.f;
	};

	/**
	 * Le temps qu'il fait à cet instant. Forced (triche) fixe le type : le temps continu prend une
	 * valeur typique de ce type, pluie, vent et averses continuent de varier.
	 */
	EREWHON_API FState Sample(double WorldClock, int32 WorldSeed, TOptional<EKind> Forced = {});

	EREWHON_API FText KindName(EKind Kind);

	/** Tests : -ErewhonWeather=0.9 fige le temps continu (0 → 1), -ErewhonHumidity=0.8 l'humidité. */
	EREWHON_API void ReadTestOverrides();
}
