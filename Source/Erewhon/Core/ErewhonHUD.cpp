#include "Core/ErewhonHUD.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonUserSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/Pawn.h"
#include "Player/ErewhonCharacter.h"
#include "Sky/CelestialSystem.h"
#include "World/SectorTypes.h"

#define LOCTEXT_NAMESPACE "ErewhonHUD"

namespace
{
	const FLinearColor GTextColor = FLinearColor::FromSRGBColor(FColor(255, 250, 235));
	const FLinearColor GCoinColor = FLinearColor::FromSRGBColor(FColor(255, 214, 102));
	const FLinearColor GWarnColor = FLinearColor::FromSRGBColor(FColor(255, 170, 140));
}

void AErewhonHUD::DrawShadowedText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale, bool bCentered)
{
	UFont* Font = GEngine->GetMediumFont();
	if (bCentered)
	{
		float Width = 0.f, Height = 0.f;
		GetTextSize(Text, Width, Height, Font, Scale);
		X -= Width * 0.5f;
	}
	DrawText(Text, FLinearColor(0, 0, 0, 0.6f), X + 1.5f, Y + 1.5f, Font, Scale);
	DrawText(Text, Color, X, Y, Font, Scale);
}

void AErewhonHUD::DrawHUD()
{
	Super::DrawHUD();

	const AErewhonPlayerController* Controller = Cast<AErewhonPlayerController>(GetOwningPlayerController());
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!Controller || !State || !Canvas)
	{
		return;
	}
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
	const AErewhonCharacter* Character = Cast<AErewhonCharacter>(Controller->GetPawn());

	// Réticule et cible.
	if (!Controller->IsMenuOpen() && Character)
	{
		DrawRect(FLinearColor(1, 1, 1, 0.85f), CX - 2.f, CY - 2.f, 4.f, 4.f);
		const FCharacterTarget& Target = Character->GetTarget();
		if (!Target.Hint.IsEmpty())
		{
			DrawShadowedText(Target.Hint.ToString(), CX, CY + 22.f, Target.bPlantHarvestable || Target.Interactable.IsValid() ? GTextColor : GWarnColor, 1.f, true);
		}
	}

	// Pièces et secteur courant, en haut à gauche.
	float Y = 18.f;
	if (PlayerState)
	{
		DrawShadowedText(FString::Printf(TEXT("%d C"), PlayerState->GetCoins()), 20.f, Y, GCoinColor, 1.3f);
		Y += 30.f;
	}
	if (Character)
	{
		const FIntPoint Sector = ErewhonSector::FromWorld(Character->GetActorLocation());
		const ESectorType Type = ErewhonSector::TypeAt(Sector, State->GetWorldSeed());
		FText OwnerText;
		if (!ErewhonSector::IsPurchasable(Sector))
		{
			OwnerText = LOCTEXT("MerchantOwner", "le marchand");
		}
		else if (const FString* Key = State->FindSectorOwnerKey(Sector))
		{
			OwnerText = PlayerState && *Key == PlayerState->GetPlayerKey()
				? LOCTEXT("YouOwner", "vous")
				: FText::FromString(State->GetSectorOwnerName(Sector));
		}
		else
		{
			OwnerText = LOCTEXT("FreeOwner", "libre");
		}
		const FText Line = FText::Format(LOCTEXT("SectorLine", "Secteur {0} · {1} · {2}"),
			FText::FromString(ErewhonSector::ToString(Sector)), ErewhonSector::TypeLabel(Type), OwnerText);
		DrawShadowedText(Line.ToString(), 20.f, Y, GTextColor, 0.9f);
	}

	// Heures du Soleil et d'Usha, sous la ligne du secteur.
	if (const ACelestialSystem* Sky = ACelestialSystem::Find(this))
	{
		const double Clock = State->GetWorldClock();
		const FString Line = FString::Printf(TEXT("Soleil %s   Usha %s"),
			*ACelestialSystem::ClockString(Sky->GetSunOrbit(), Clock), *ACelestialSystem::ClockString(Sky->GetUshaOrbit(), Clock));
		DrawShadowedText(Line, 20.f, Y + 24.f, GTextColor, 0.9f);
	}

	// Images par seconde, lissées, en haut à droite.
	const UErewhonUserSettings* User = UErewhonUserSettings::Get();
	const float Delta = GetWorld()->GetDeltaSeconds();
	if (Delta > 0.f)
	{
		SmoothedFps = SmoothedFps <= 0.f ? 1.f / Delta : FMath::Lerp(SmoothedFps, 1.f / Delta, 0.05f);
	}
	if (User && User->bShowFps)
	{
		const FString Fps = FString::Printf(TEXT("%d i/s"), FMath::RoundToInt32(SmoothedFps));
		float Width = 0.f, Height = 0.f;
		GetTextSize(Fps, Width, Height, GEngine->GetMediumFont(), 0.9f);
		const FLinearColor FpsColor = SmoothedFps >= 50.f ? GTextColor : SmoothedFps >= 30.f ? GCoinColor : GWarnColor;
		DrawShadowedText(Fps, Canvas->ClipX - Width - 20.f, 18.f, FpsColor, 0.9f);
	}

	// Messages temporaires, au-dessus du bas de l'écran.
	const double Now = GetWorld()->GetRealTimeSeconds();
	float ToastY = Canvas->ClipY - 140.f;
	for (int32 i = Controller->GetToasts().Num() - 1; i >= 0; --i)
	{
		const FErewhonToast& Toast = Controller->GetToasts()[i];
		if (Toast.ExpireTime > Now)
		{
			DrawShadowedText(Toast.Text.ToString(), CX, ToastY, GWarnColor, 1.f, true);
			ToastY -= 26.f;
		}
	}
}

#undef LOCTEXT_NAMESPACE
