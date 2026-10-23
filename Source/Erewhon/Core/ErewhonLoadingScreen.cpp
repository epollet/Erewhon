#include "Core/ErewhonLoadingScreen.h"

#include "AssetCompilingManager.h"
#include "ContentStreaming.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PipelineStateCache.h"
#include "ShaderCompiler.h"
#include "UI/SLoadingOverlay.h"
#include "UObject/UObjectGlobals.h"
#include "World/SectorManager.h"

#define LOCTEXT_NAMESPACE "ErewhonLoadingScreen"

DEFINE_LOG_CATEGORY_STATIC(LogErewhonLoading, Log, All);

namespace
{
	/** Au-delà, on montre le monde quoi qu'il reste à charger. */
	constexpr double GLoaderTimeoutSeconds = 30.0;
	/** Images régulières exigées avant de lever l'écran. */
	constexpr float GLoaderStableSeconds = 1.f;
	constexpr double GLoaderFadeSeconds = 0.6;
	/** Textures encore attendues qu'on tolère (le budget de streaming en laisse toujours quelques-unes)… */
	constexpr int32 GLoaderTextureSlack = 16;
	/** … ou dont le nombre ne baisse plus depuis ce temps. */
	constexpr double GLoaderTextureStallSeconds = 3.0;
	/** Au-dessus des menus (10), sous la popup de signalement (100). */
	constexpr int32 GLoaderZOrder = 50;

	bool IsLoaderSkipped()
	{
		// Éditeur (PIE) : le monde tout de suite. Captures automatiques : elles ont leur propre attente
		// (-ErewhonShotDelay), et l'écran de chargement apparaîtrait sur les images.
		FString Shots;
		return GIsEditor || IsRunningDedicatedServer()
			|| FParse::Param(FCommandLine::Get(), TEXT("ErewhonNoLoader"))
			|| FParse::Value(FCommandLine::Get(), TEXT("ErewhonShots="), Shots, false);
	}
}

void UErewhonLoadingScreen::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!IsLoaderSkipped())
	{
		PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UErewhonLoadingScreen::OnPostLoadMap);
	}
}

void UErewhonLoadingScreen::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	Hide();
	Super::Deinitialize();
}

bool UErewhonLoadingScreen::IsBlockingInput(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const UErewhonLoadingScreen* Loader = Instance ? Instance->GetSubsystem<UErewhonLoadingScreen>() : nullptr;
	// Pendant le fondu, le joueur reprend déjà la main.
	return Loader && Loader->Overlay.IsValid() && Loader->FadeStartedAt < 0.0;
}

void UErewhonLoadingScreen::OnPostLoadMap(UWorld* World)
{
	// La carte est chargée mais pas encore affichée : l'écran passe devant dès la première image.
	if (World && World->GetGameInstance() == GetGameInstance() && World->GetNetMode() != NM_DedicatedServer)
	{
		Show();
	}
}

void UErewhonLoadingScreen::Show()
{
	UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (!Viewport)
	{
		return;
	}
	if (!Overlay.IsValid())
	{
		Overlay = SNew(SLoadingOverlay)
			.Status_UObject(this, &UErewhonLoadingScreen::GetStatus)
			.Progress_UObject(this, &UErewhonLoadingScreen::GetProgress);
		Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), GLoaderZOrder);
	}
	Overlay->SetRenderOpacity(1.f);

	ShownAt = FPlatformTime::Seconds();
	FadeStartedAt = -1.0;
	Status = LOCTEXT("StatusWorld", "Préparation du monde…");
	Progress = 0.f;
	TargetProgress = 0.f;
	AverageFrame = 0.f;
	StableSeconds = 0.f;
	PeakShaders = 0;
	PeakTextures = 0;
	LastTextures = TNumericLimits<int32>::Max();
	TexturesMovedAt = ShownAt;
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UErewhonLoadingScreen::TickLoader));
	}
	UE_LOG(LogErewhonLoading, Log, TEXT("Écran de chargement affiché"));
}

void UErewhonLoadingScreen::Hide()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (Overlay.IsValid())
	{
		if (UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
		Overlay.Reset();
	}
}

bool UErewhonLoadingScreen::TickLoader(float DeltaTime)
{
	if (!Overlay.IsValid())
	{
		TickerHandle.Reset();
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (FadeStartedAt >= 0.0)
	{
		const float Alpha = 1.f - static_cast<float>((Now - FadeStartedAt) / GLoaderFadeSeconds);
		if (Alpha <= 0.f)
		{
			Hide();
			return false;
		}
		Overlay->SetRenderOpacity(FMath::SmoothStep(0.f, 1.f, Alpha));
		return true;
	}

	const bool bReady = Evaluate(DeltaTime);
	Progress = FMath::Max(Progress, FMath::FInterpTo(Progress, TargetProgress, DeltaTime, 3.f));
	const bool bTimedOut = Now - ShownAt > GLoaderTimeoutSeconds;
	if (bReady || bTimedOut)
	{
		if (bReady)
		{
			UE_LOG(LogErewhonLoading, Log, TEXT("Monde prêt en %.1f s"), Now - ShownAt);
		}
		else
		{
			UE_LOG(LogErewhonLoading, Warning, TEXT("Écran de chargement levé après %.0f s sans attendre la fin : %s"), GLoaderTimeoutSeconds, *Status.ToString());
		}
		Progress = 1.f;
		FadeStartedAt = Now;
	}
	return true;
}

bool UErewhonLoadingScreen::Evaluate(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	// Images régulières : aucune ne dépasse nettement la moyenne (un à-coup de chargement remet à zéro).
	const float Frame = FMath::Min(DeltaTime, 1.f);
	AverageFrame = AverageFrame <= 0.f ? Frame : FMath::Lerp(AverageFrame, Frame, 0.1f);
	const bool bSmooth = Frame < FMath::Max(0.05f, AverageFrame * 1.6f);

	// 1. Le joueur local et son pawn (chez un hôte : répliqués après la carte).
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const APlayerController* LocalController = World ? World->GetFirstPlayerController() : nullptr;
	const bool bPawn = World && World->HasBegunPlay() && LocalController && LocalController->GetPawn();

	// 2. Les secteurs autour de lui, plantes comprises, et l'anneau lointain.
	int32 SectorsPending = 0;
	int32 SectorsWanted = 0;
	const bool bSectorsKnown = bPawn && ASectorManager::GetLocalLoadState(World, SectorsPending, SectorsWanted);
	const bool bNoManager = bPawn && !TActorIterator<ASectorManager>(World);
	const bool bSectors = bNoManager || (bSectorsKnown && SectorsPending == 0);

	// 3. Shaders, assets compilés à la volée (jeu lancé hors cuisson) et états de pipeline préparés.
	int32 Shaders = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
	Shaders += FAssetCompilingManager::Get().GetNumRemainingAssets();
	Shaders += static_cast<int32>(PipelineStateCache::NumActivePrecacheRequests());
	PeakShaders = FMath::Max(PeakShaders, Shaders);

	// 4. Les textures : suivies une fois les secteurs posés (avant, presque rien n'est demandé).
	const int32 Textures = IStreamingManager::Get().GetNumWantingResources();
	if (!bSectors || Textures < LastTextures)
	{
		LastTextures = bSectors ? Textures : TNumericLimits<int32>::Max();
		TexturesMovedAt = Now;
	}
	if (bSectors)
	{
		PeakTextures = FMath::Max(PeakTextures, Textures);
	}
	const bool bTextures = bSectors && (Textures <= GLoaderTextureSlack || Now - TexturesMovedAt > GLoaderTextureStallSeconds);

	// 5. Tout est là : encore une seconde d'images régulières.
	const bool bLoaded = bPawn && bSectors && Shaders == 0 && bTextures;
	StableSeconds = bLoaded && bSmooth ? StableSeconds + Frame : 0.f;

	if (!bPawn || (!bSectorsKnown && !bNoManager))
	{
		Status = LOCTEXT("StatusWorld", "Préparation du monde…");
	}
	else if (!bSectors)
	{
		Status = FText::Format(LOCTEXT("StatusSectors", "Les environs prennent forme… ({0})"), SectorsPending);
	}
	else if (Shaders > 0)
	{
		Status = FText::Format(LOCTEXT("StatusShaders", "Compilation des shaders… ({0})"), Shaders);
	}
	else if (!bTextures)
	{
		Status = LOCTEXT("StatusTextures", "Chargement des textures…");
	}
	else
	{
		Status = LOCTEXT("StatusSettle", "Stabilisation…");
	}

	// Avancement : le pawn, puis les secteurs (le gros du travail), puis shaders, textures et stabilité.
	float Estimate = bPawn ? 0.1f : 0.f;
	if (bSectors)
	{
		Estimate = 0.6f;
		Estimate += 0.2f * (PeakShaders > 0 ? 1.f - static_cast<float>(Shaders) / PeakShaders : 1.f);
		if (Shaders == 0)
		{
			Estimate += 0.1f * (bTextures || PeakTextures <= 0 ? 1.f : 1.f - static_cast<float>(Textures) / PeakTextures);
		}
		if (bLoaded)
		{
			Estimate += 0.1f * FMath::Min(1.f, StableSeconds / GLoaderStableSeconds);
		}
	}
	else if (bSectorsKnown && SectorsWanted > 0)
	{
		Estimate += 0.5f * (1.f - static_cast<float>(SectorsPending) / SectorsWanted);
	}
	TargetProgress = FMath::Max(TargetProgress, FMath::Clamp(Estimate, 0.f, 1.f));

	return bLoaded && StableSeconds >= GLoaderStableSeconds;
}

#undef LOCTEXT_NAMESPACE
