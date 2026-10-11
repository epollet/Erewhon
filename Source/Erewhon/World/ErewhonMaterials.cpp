#include "World/ErewhonMaterials.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "World/ErewhonVisuals.h"

UMaterialInstanceDynamic* UErewhonMaterials::GetFoliage()
{
	if (!bFoliageLoaded)
	{
		bFoliageLoaded = true;
		if (UMaterialInterface* Base = ErewhonVisuals::LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Foliage")))
		{
			Foliage = UMaterialInstanceDynamic::Create(Base, this);
		}
	}
	return Foliage;
}

void UErewhonMaterials::SetWind(float Intensity, float Speed)
{
	if (UMaterialInstanceDynamic* Material = GetFoliage())
	{
		Material->SetScalarParameterValue(TEXT("WindIntensity"), Intensity);
		Material->SetScalarParameterValue(TEXT("WindSpeed"), Speed);
	}
}
