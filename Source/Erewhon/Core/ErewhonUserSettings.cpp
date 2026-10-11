#include "Core/ErewhonUserSettings.h"

#include "Engine/Engine.h"

UErewhonUserSettings* UErewhonUserSettings::Get()
{
	return GEngine ? Cast<UErewhonUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UErewhonUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	ViewDistanceSectors = 2;
	VegetationDensity = 1.f;
	VegetationDistance = 1.f;
	bVegetationShadows = true;
	bShowFps = true;
	CloudQuality = 1;
	EffectsVolume = 0.8f;
	AmbientVolume = 0.6f;
	MusicVolume = 0.5f;
}

void UErewhonUserSettings::NotifyWorldDetailChanged()
{
	ViewDistanceSectors = FMath::Clamp(ViewDistanceSectors, 1, 3);
	VegetationDensity = FMath::Clamp(VegetationDensity, 0.f, 2.f);
	VegetationDistance = FMath::Clamp(VegetationDistance, 0.3f, 2.f);
	CloudQuality = FMath::Clamp(CloudQuality, 0, 2);
	SaveSettings();
	OnWorldDetailChanged.Broadcast();
}
