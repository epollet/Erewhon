#include "World/MusicPlayer.h"

#include "Components/AudioComponent.h"
#include "Core/ErewhonUserSettings.h"
#include "Sound/SoundWave.h"
#include "World/ErewhonVisuals.h"

namespace
{
	constexpr double GFirstDelay = 8.0;    // s après l'arrivée en jeu
	constexpr float GFade = 5.f;           // s de fondu, à l'entrée comme à la sortie
	constexpr double GGapMin = 45.0;       // silence entre deux morceaux (s)
	constexpr double GGapMax = 120.0;
	constexpr int32 GMaxTracks = 16;
}

AMusicPlayer::AMusicPlayer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMusicPlayer::BeginPlay()
{
	Super::BeginPlay();
	Rand.GenerateNewSeed();

	// S_music_ambient_000, _001… importés depuis SourceArt/Music.
	for (int32 i = 0; i < GMaxTracks; ++i)
	{
		if (USoundWave* Track = ErewhonVisuals::LoadGenerated<USoundWave>(TEXT("Music"), FString::Printf(TEXT("S_music_ambient_%03d"), i)))
		{
			Tracks.Add(Track);
		}
	}

	Audio = NewObject<UAudioComponent>(this);
	Audio->SetupAttachment(RootComponent);
	Audio->bAllowSpatialization = false;
	Audio->bAutoActivate = false;
	Audio->bIsUISound = true;   // continue pendant les menus
	Audio->RegisterComponent();

	NextStart = GetWorld()->GetTimeSeconds() + GFirstDelay;
}

float AMusicPlayer::TargetVolume() const
{
	const UErewhonUserSettings* User = UErewhonUserSettings::Get();
	return User ? User->MusicVolume : 0.5f;
}

void AMusicPlayer::StartTrack(double Now)
{
	// Au hasard, mais jamais deux fois le même morceau de suite.
	int32 Index = Rand.RandHelper(Tracks.Num());
	if (Tracks.Num() > 1 && Index == LastTrack)
	{
		Index = (Index + 1 + Rand.RandHelper(Tracks.Num() - 1)) % Tracks.Num();
	}
	LastTrack = Index;
	Audio->SetSound(Tracks[Index]);
	AppliedVolume = TargetVolume();
	Audio->FadeIn(GFade, AppliedVolume);
	TrackEnd = Now + Tracks[Index]->Duration;
	UE_LOG(LogTemp, Log, TEXT("Musique : %s (%.0f s)"), *Tracks[Index]->GetName(), Tracks[Index]->Duration);
	bPlaying = true;
	bFadingOut = false;
}

void AMusicPlayer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Tracks.IsEmpty() || !Audio)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bPlaying)
	{
		if (Now >= NextStart)
		{
			StartTrack(Now);
		}
		return;
	}

	// Le réglage du volume suit les options, en douceur.
	const float Volume = TargetVolume();
	if (!bFadingOut && !FMath::IsNearlyEqual(Volume, AppliedVolume, 0.01f))
	{
		AppliedVolume = Volume;
		Audio->AdjustVolume(0.5f, Volume);
	}
	if (!bFadingOut && Now >= TrackEnd - GFade)
	{
		bFadingOut = true;
		Audio->FadeOut(GFade, 0.f);
	}
	if (Now >= TrackEnd + 0.5)
	{
		bPlaying = false;
		Audio->Stop();
		NextStart = Now + Rand.FRandRange(GGapMin, GGapMax);
	}
}
