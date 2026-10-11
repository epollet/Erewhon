#pragma once

#include "CoreMinimal.h"

class USoundWave;
class USoundAttenuation;

/**
 * Sons générés (ElevenLabs), importés en /Game/Generated/Sounds/S_<nom>.
 * Les volumes suivent les réglages du joueur : effets et ambiance.
 */
namespace ErewhonSound
{
	enum class EChannel : uint8 { Effects, Ambient };

	EREWHON_API USoundWave* Load(const FString& Name);

	/** Un nom au hasard parmi Prefix_1 … Prefix_Count. */
	EREWHON_API USoundWave* LoadVariant(const FString& Prefix, int32 Count);

	/** Volume réglé par le joueur pour ce canal (0 → 1). */
	EREWHON_API float ChannelVolume(EChannel Channel);

	/** Atténuation partagée : audible jusqu'à FalloffCm, nette en deçà de InnerCm. */
	EREWHON_API USoundAttenuation* Attenuation(float InnerCm, float FalloffCm);

	/** Joue un son dans le monde, légèrement varié en hauteur. */
	EREWHON_API void PlayAt(const UObject* WorldContext, USoundWave* Sound, const FVector& Location, float Volume,
		float InnerCm, float FalloffCm, EChannel Channel = EChannel::Effects);
}
