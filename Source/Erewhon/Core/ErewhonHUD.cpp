#include "Core/ErewhonHUD.h"
#include "Inventory/BackpackComponent.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonUserSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Inventory/InventoryComponent.h"
#include "Items/ItemDatabase.h"
#include "GameFramework/Pawn.h"
#include "Player/ErewhonCharacter.h"
#include "Sky/CelestialSystem.h"
#include "World/DroppedItem.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
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

	// Les objets posés à terre, tout près : leur nom et le temps qui leur reste avant de disparaître.
	if (Character && !Controller->IsMenuOpen())
	{
		for (TActorIterator<ADroppedItem> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist(It->GetActorLocation(), Character->GetActorLocation()) > 1200.f)
			{
				continue;
			}
			FVector2D Screen;
			if (GetOwningPlayerController()->ProjectWorldLocationToScreen(It->GetActorLocation() + FVector(0.f, 0.f, 45.f), Screen, true))
			{
				// Coordonnées du viewport → coordonnées du Canvas (identiques, sauf mise à l'échelle).
				FVector2D ViewSize;
				GetWorld()->GetGameViewport()->GetViewportSize(ViewSize);
				const float ToCanvas = ViewSize.X > 0.f ? Canvas->ClipX / ViewSize.X : 1.f;
				DrawShadowedText(It->GetLabel(), Screen.X * ToCanvas, Screen.Y * ToCanvas, It->GetSecondsLeft() < 30.0 ? GWarnColor : GTextColor, 0.8f, true);
			}
		}
	}

	// Au pichet : une barre par litre, de gauche à droite quand on puise, de droite à gauche quand on verse.
	if (const FErewhonCraftJob& Job = Controller->GetCraftJob(); Job.IsActive() && Job.Pour != 0 && !Controller->IsMenuOpen())
	{
		const double Left = Job.StepEnd - State->GetWorldClock();
		const float Done = FMath::Clamp(1.f - static_cast<float>(Left / FMath::Max(0.01f, Job.StepSeconds)), 0.f, 1.f);
		constexpr float Width = 180.f, Height = 12.f;
		const float X = CX - Width * 0.5f, Top = CY + 52.f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 2.f, Top - 2.f, Width + 4.f, Height + 4.f);
		const FLinearColor Liquid(0.45f, 0.7f, 1.f, 0.95f);
		if (Job.Pour != 2)
		{
			DrawRect(Job.Pour == 3 ? FLinearColor(0.75f, 0.55f, 0.32f, 0.95f) : Liquid, X, Top, Width * Done, Height);
		}
		else
		{
			DrawRect(Liquid, X + Width * (1.f - Done), Top, Width * Done, Height);
		}
		DrawShadowedText(Job.Pour == 7 ? TEXT("On détache une bûche…") : Job.Pour == 4 ? TEXT("On cueille…") : Job.Pour == 5 ? TEXT("On coupe…") : Job.Pour == 3 ? TEXT("On bêche…") : Job.Pour == 1 ? TEXT("On puise un litre…") : TEXT("On verse un litre…"), CX, Top + Height + 4.f, GTextColor, 0.75f, true);
	}

	// Pièces et secteur courant, en haut à gauche.
	float Y = 18.f;
	if (PlayerState)
	{
		DrawShadowedText(FString::Printf(TEXT("%d C"), PlayerState->GetCoins()), 20.f, Y, GCoinColor, 1.3f);
		Y += 30.f;
		if (Character)
		{
			const float Load = Character->GetCarriedWeight();
			const float Comfort = Character->GetCarryComfort();
			const FLinearColor LoadColor = Load >= Character->GetCarryMax() ? FLinearColor(1.f, 0.35f, 0.3f) : Load > Comfort ? GWarnColor : GTextColor;
			const FString Note = Load >= Character->GetCarryMax() ? TEXT("  — trop lourd, impossible d'avancer") : Load > Comfort ? TEXT("  — chargé, vous ralentissez") : TEXT("");
			DrawShadowedText(FString::Printf(TEXT("Charge %.1f / %.0f kg%s"), Load, Comfort, *Note), 20.f, Y, LoadColor, 0.85f);
			Y += 24.f;
		}
	}
	if (Character)
	{
		const FIntPoint Sector = ErewhonSector::FromWorld(Character->GetActorLocation());
		const ESectorType Type = ErewhonSector::TypeAt(Sector, State->GetWorldSeed());
		FText OwnerText;
		if (Type == ESectorType::Spawn || Type == ESectorType::Road)
		{
			OwnerText = LOCTEXT("PublicOwner", "public");   // commerces et routes
		}
		else if (const FString* Key = State->FindSectorOwnerKey(Sector))
		{
			OwnerText = PlayerState && *Key == PlayerState->GetPlayerKey()
				? LOCTEXT("YouOwner", "à vous")
				: FText::Format(LOCTEXT("PrivateOwner", "privé ({0})"), FText::FromString(State->GetSectorOwnerName(Sector)));
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
		const FString Line = FString::Printf(TEXT("Soleil %s   Usha %s   ·   %s"),
			*ACelestialSystem::ClockString(Sky->GetSunOrbit(), Clock), *ACelestialSystem::ClockString(Sky->GetUshaOrbit(), Clock),
			*ErewhonWeather::KindName(Sky->GetWeather().Kind).ToString());
		DrawShadowedText(Line, 20.f, Y + 24.f, GTextColor, 0.9f);
	}

	// Le sac sur le dos : on rappelle comment le poser.
	if (const UBackpackComponent* Bag = PlayerState ? PlayerState->GetBackpack() : nullptr; Bag && Bag->IsWorn())
	{
		DrawShadowedText(TEXT("Sac sur le dos : [F] pour le poser"), 20.f, Y + 48.f, GTextColor, 0.8f);
	}

	// Potions bues : leur effet et le temps qui reste, en haut à droite sous les images par seconde.
	if (Character)
	{
		static const TCHAR* const Names[] = { TEXT(""), TEXT("Vitesse"), TEXT("Nyctalopie"), TEXT("Force"), TEXT("Saut"), TEXT("Rapetissement"), TEXT("Grandissement"), TEXT("Sérénité"), TEXT("Lumière") };
		float PotionY = 48.f;
		for (int32 Effect = 1; Effect < static_cast<int32>(EPotionEffect::Count); ++Effect)
		{
			const float Remaining = Character->PotionRemaining(static_cast<EPotionEffect>(Effect));
			if (Remaining <= 0.f)
			{
				continue;
			}
			const int32 Seconds = FMath::CeilToInt32(Remaining);
			const FString Line = FString::Printf(TEXT("%s  %d:%02d"), Names[Effect], Seconds / 60, Seconds % 60);
			float Width = 0.f, Height = 0.f;
			GetTextSize(Line, Width, Height, GEngine->GetMediumFont(), 0.9f);
			DrawShadowedText(Line, Canvas->ClipX - Width - 20.f, PotionY, GCoinColor, 0.9f);
			PotionY += 22.f;
		}
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

	// La barre d'équipement, en bas au milieu : trois emplacements, celui en main en surbrillance.
	if (PlayerState && !Controller->IsMenuOpen())
	{
		const UInventoryComponent* Inventory = PlayerState->GetInventory();
		const UItemDatabase* Items = UItemDatabase::Get(this);
		const int32 Size = Inventory->GetHotbarSize();
		constexpr float Box = 58.f, Gap = 10.f;
		const float Left = CX - (Size * Box + (Size - 1) * Gap) * 0.5f;
		const float Top = Canvas->ClipY - Box - 22.f;
		for (int32 Hotbar = 0; Hotbar < Size; ++Hotbar)
		{
			const bool bActive = Hotbar == Inventory->GetActiveHotbar();
			const float X = Left + Hotbar * (Box + Gap);
			const float Border = bActive ? 3.f : 1.5f;
			DrawRect(bActive ? GCoinColor : FLinearColor(1.f, 1.f, 1.f, 0.35f), X - Border, Top - Border, Box + 2.f * Border, Box + 2.f * Border);
			DrawRect(bActive ? FLinearColor(0.16f, 0.12f, 0.07f, 0.82f) : FLinearColor(0.f, 0.f, 0.f, 0.5f), X, Top, Box, Box);
			const int32 Index = Inventory->HotbarSlotIndex(Hotbar);
			const FInventorySlot* Slot = Inventory->GetSlots().IsValidIndex(Index) ? &Inventory->GetSlots()[Index] : nullptr;
			const FItemDef* Def = Slot && !Slot->IsEmpty() && Items ? Items->Find(Slot->ItemId) : nullptr;
			if (Def)
			{
				if (UTexture2D* Icon = Cast<UTexture2D>(Def->Icon.TryLoad()))
				{
					DrawTexture(Icon, X + 4.f, Top + 4.f, Box - 8.f, Box - 8.f, 0.f, 0.f, 1.f, 1.f);
				}
				else
				{
					DrawRect(Def->Color, X + 14.f, Top + 14.f, Box - 28.f, Box - 28.f);
				}
				if (Slot->Count > 1)
				{
					DrawShadowedText(FString::FromInt(Slot->Count), X + Box - 18.f, Top + Box - 22.f, GTextColor, 0.8f);
				}
			}
			DrawShadowedText(FString::FromInt(Hotbar + 1), X + 4.f, Top + 1.f, bActive ? GCoinColor : GTextColor, 0.7f);
		}
		// Le nom de l'objet en main, au-dessus de la barre.
		if (const FItemDef* Hand = Items ? Items->Find(Inventory->GetHandItem()) : nullptr)
		{
			DrawShadowedText(Hand->Name.ToString(), CX, Top - 28.f, GTextColor, 0.85f, true);
		}
	}

	// Messages temporaires, au-dessus du bas de l'écran.
	const double Now = GetWorld()->GetRealTimeSeconds();
	float ToastY = Canvas->ClipY - 150.f;
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
