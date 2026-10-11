#include "World/ErewhonSound.h"

#include "Core/ErewhonUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "World/ErewhonVisuals.h"

namespace ErewhonSound
{
	USoundWave* Load(const FString& Name)
	{
		// Cache : les sons de pas se demandent plusieurs fois par seconde.
		static TMap<FString, TWeakObjectPtr<USoundWave>> Cache;
		if (const TWeakObjectPtr<USoundWave>* Found = Cache.Find(Name); Found && Found->IsValid())
		{
			return Found->Get();
		}
		USoundWave* Sound = ErewhonVisuals::LoadGenerated<USoundWave>(TEXT("Sounds"), TEXT("S_") + Name);
		Cache.Add(Name, Sound);
		return Sound;
	}

	USoundWave* LoadVariant(const FString& Prefix, int32 Count)
	{
		return Load(FString::Printf(TEXT("%s_%d"), *Prefix, FMath::RandRange(1, FMath::Max(1, Count))));
	}

	float ChannelVolume(EChannel Channel)
	{
		const UErewhonUserSettings* User = UErewhonUserSettings::Get();
		if (!User)
		{
			return 1.f;
		}
		return Channel == EChannel::Ambient ? User->AmbientVolume : User->EffectsVolume;
	}

	USoundAttenuation* Attenuation(float InnerCm, float FalloffCm)
	{
		static TMap<FIntPoint, USoundAttenuation*> Cache;
		const FIntPoint Key(FMath::RoundToInt32(InnerCm), FMath::RoundToInt32(FalloffCm));
		if (USoundAttenuation** Found = Cache.Find(Key))
		{
			return *Found;
		}
		USoundAttenuation* Settings = NewObject<USoundAttenuation>(GetTransientPackage());
		Settings->AddToRoot();   // vit toute la session
		Settings->Attenuation.bAttenuate = true;
		Settings->Attenuation.bSpatialize = true;
		Settings->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Settings->Attenuation.AttenuationShapeExtents = FVector(InnerCm, 0.f, 0.f);
		Settings->Attenuation.FalloffDistance = FalloffCm;
		Settings->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Cache.Add(Key, Settings);
		return Settings;
	}

	void PlayAt(const UObject* WorldContext, USoundWave* Sound, const FVector& Location, float Volume,
		float InnerCm, float FalloffCm, EChannel Channel)
	{
		const float Final = Volume * ChannelVolume(Channel);
		if (!Sound || Final <= 0.f || !WorldContext)
		{
			return;
		}
		UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator, Final,
			FMath::FRandRange(0.92f, 1.08f), 0.f, Attenuation(InnerCm, FalloffCm));
	}
}
