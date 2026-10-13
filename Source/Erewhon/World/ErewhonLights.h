#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ErewhonLights.generated.h"

class ULightComponent;
class UMaterialInstanceDynamic;

/**
 * Les lanternes du village : leurs lumières et leurs verres s'allument à la tombée du jour
 * et vacillent comme une flamme de bougie, chacune à son rythme.
 * Le ciel donne la part de nuit (0 le jour, 1 la nuit).
 */
UCLASS()
class EREWHON_API UErewhonLights : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Une lumière qui brille autour de MaxIntensity la nuit, éteinte le jour. */
	void RegisterLight(ULightComponent* Light, float MaxIntensity, float Phase = -1.f);

	/** Un verre (paramètre « Intensity » de M_Emissive) qui luit la nuit. */
	void RegisterGlow(UMaterialInstanceDynamic* Glow, float MaxIntensity, float Phase = -1.f);

	void SetNight(float InNight) { Night = FMath::Clamp(InNight, 0.f, 1.f); }
	float GetNight() const { return Night; }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UErewhonLights, STATGROUP_Tickables); }

private:
	/** Vacillement d'une flamme (autour de 1), propre à chaque lanterne par sa phase. */
	float Flicker(float Phase) const;

	struct FLight { TWeakObjectPtr<ULightComponent> Light; float MaxIntensity = 0.f; float Phase = 0.f; bool bVisible = true; };
	struct FGlow { TWeakObjectPtr<UMaterialInstanceDynamic> Glow; float MaxIntensity = 0.f; float Phase = 0.f; };
	TArray<FLight> Lights;
	TArray<FGlow> Glows;
	float Night = 0.f;
	float Time = 0.f;
};
