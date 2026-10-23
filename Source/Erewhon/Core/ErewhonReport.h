#pragma once

#include "CoreMinimal.h"

class AErewhonPlayerController;

/**
 * Signalements en jeu (AltGr+²) : l'écran et l'état du jeu au moment de l'appui, avec la
 * description du joueur, dans un dossier par cas sous Reports/ (report.md, context.json,
 * screenshot.png, log.txt) — de quoi retrouver et corriger le problème plus tard.
 */
namespace ErewhonReport
{
	enum class ECategory : uint8 { Bug, Observation, Idea };

	FText CategoryLabel(ECategory Category);

	/** Une rubrique du contexte : des paires clé / valeur, dans l'ordre. */
	struct FSection
	{
		FString Name;
		TArray<TPair<FString, FString>> Values;

		void Add(const FString& Key, const FString& Value) { Values.Emplace(Key, Value); }
	};

	struct FCapture
	{
		FDateTime Time;
		/** L'écran, interface comprise ; vide si la capture a échoué. */
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		TArray<FSection> Context;

		/** Une valeur du contexte (vide si absente), pour le résumé de la popup. */
		FString Find(const FString& Section, const FString& Key) const;
	};

	/** Fige l'écran et l'état du jeu vu par ce joueur. */
	FCapture Capture(AErewhonPlayerController* Controller);

	/** Écrit le dossier du signalement ; renvoie son chemin absolu, vide en cas d'échec. */
	FString Save(const FCapture& Capture, ECategory Category, const FString& Title, const FString& Description, bool bWithScreenshot);
}
