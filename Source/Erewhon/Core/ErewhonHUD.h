#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ErewhonHUD.generated.h"

/** HUD de jeu dessiné au Canvas : réticule, cible, pièces, secteur, heures, messages. */
UCLASS()
class EREWHON_API AErewhonHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawShadowedText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bCentered = false);
};
