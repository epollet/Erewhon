#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ErewhonLoadingScreen.generated.h"

class SLoadingOverlay;

/**
 * Écran de chargement, posé sur la vue dès qu'une carte est chargée (démarrage, arrivée chez un hôte).
 * Il cache le monde tant qu'il se construit : pawn local, secteurs autour du joueur, shaders,
 * textures, puis une seconde d'images régulières ; il s'efface ensuite en fondu.
 * Jamais plus de GLoaderTimeoutSeconds. Absent dans l'éditeur (PIE) et pour les captures automatiques.
 */
UCLASS()
class EREWHON_API UErewhonLoadingScreen : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Vrai tant que l'écran cache le monde : le contrôleur ne transmet alors rien au jeu. */
	static bool IsBlockingInput(const UObject* WorldContext);

	/** Ligne d'état et avancement (0 à 1), lus par le widget. */
	FText GetStatus() const { return Status; }
	float GetProgress() const { return Progress; }

private:
	void OnPostLoadMap(UWorld* World);
	void Show();
	void Hide();
	bool TickLoader(float DeltaTime);
	/** Le monde est-il prêt ? Met à jour Status et Progress au passage. */
	bool Evaluate(float DeltaTime);

	TSharedPtr<SLoadingOverlay> Overlay;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle PostLoadHandle;

	FText Status;
	float Progress = 0.f;
	/** Avancement visé : la barre le rejoint en douceur, sans jamais reculer. */
	float TargetProgress = 0.f;
	double ShownAt = 0.0;
	/** Début du fondu de sortie (négatif : pas encore). */
	double FadeStartedAt = -1.0;

	/** Durée moyenne d'une image, et depuis combien de temps elles restent régulières. */
	float AverageFrame = 0.f;
	float StableSeconds = 0.f;

	/** Pics observés (pour un avancement en fraction) et dernière baisse des textures attendues. */
	int32 PeakShaders = 0;
	int32 PeakTextures = 0;
	int32 LastTextures = 0;
	double TexturesMovedAt = 0.0;
};
