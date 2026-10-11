#include "UI/ErewhonSlate.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Items/ItemTypes.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ErewhonUI"

namespace ErewhonUI
{
	FSlateFontInfo Font(int32 Size, bool bBold)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	const FSlateBrush* WhiteBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		// Les pinceaux vivent toute la session ; on n'en crée qu'un par combinaison.
		static TMap<FString, TUniquePtr<FSlateRoundedBoxBrush>> Cache;
		const FString Key = FString::Printf(TEXT("%s|%.1f|%s|%.1f"), *Fill.ToString(), Radius, *Outline.ToString(), OutlineWidth);
		if (const TUniquePtr<FSlateRoundedBoxBrush>* Found = Cache.Find(Key))
		{
			return Found->Get();
		}
		return Cache.Add(Key, MakeUnique<FSlateRoundedBoxBrush>(Fill, Radius, Outline, OutlineWidth)).Get();
	}

	const FButtonStyle& ButtonStyle(EButton Kind)
	{
		// Les boutons gardent un pointeur sur leur style : chaque style doit rester à la même adresse.
		static TMap<EButton, TUniquePtr<FButtonStyle>> Styles;
		if (const TUniquePtr<FButtonStyle>* Found = Styles.Find(Kind))
		{
			return **Found;
		}

		FLinearColor Fill, Outline;
		switch (Kind)
		{
		case EButton::Primary:   Fill = Leaf; Outline = Mix(Leaf, Ink, 0.35f); break;
		case EButton::Danger:    Fill = Danger; Outline = Mix(Danger, Ink, 0.35f); break;
		case EButton::TabActive: Fill = Clay; Outline = Mix(Clay, Ink, 0.3f); break;
		case EButton::Tab:       Fill = Mix(Parchment, Edge, 0.18f); Outline = Mix(Parchment, Edge, 0.5f); break;
		default:                 Fill = Card; Outline = Edge; break;
		}
		const float Radius = (Kind == EButton::Tab || Kind == EButton::TabActive) ? 14.f : 9.f;
		const FLinearColor Hover = Kind == EButton::Secondary || Kind == EButton::Tab ? Mix(Fill, Gold, 0.22f) : Mix(Fill, FLinearColor::White, 0.18f);
		const FLinearColor Pressed = Mix(Fill, Ink, 0.18f);
		const FLinearColor Disabled = Mix(Fill, Parchment, 0.6f);

		FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
		Style.SetNormal(*Rounded(Fill, Radius, Outline, 1.5f));
		Style.SetHovered(*Rounded(Hover, Radius, Outline, 1.5f));
		Style.SetPressed(*Rounded(Pressed, Radius, Outline, 1.5f));
		Style.SetDisabled(*Rounded(Disabled, Radius, Mix(Outline, Parchment, 0.5f), 1.f));
		Style.SetNormalPadding(FMargin(12.f, 6.f));
		Style.SetPressedPadding(FMargin(12.f, 7.f, 12.f, 5.f));
		return *Styles.Add(Kind, MakeUnique<FButtonStyle>(Style));
	}

	FLinearColor ButtonTextColor(EButton Kind)
	{
		return Kind == EButton::Primary || Kind == EButton::Danger || Kind == EButton::TabActive ? FLinearColor::White : Ink;
	}

	TSharedRef<SWidget> Button(const FText& Label, TFunction<void()> OnClick, EButton Kind, bool bEnabled, float MinWidth, int32 FontSize)
	{
		return SNew(SBox).MinDesiredWidth(MinWidth > 0.f ? FOptionalSize(MinWidth) : FOptionalSize())
			[
				SNew(SButton)
				.ButtonStyle(&ButtonStyle(Kind))
				.IsEnabled(bEnabled)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(Label).Font(Font(FontSize, true))
					.ColorAndOpacity(bEnabled ? ButtonTextColor(Kind) : Mix(ButtonTextColor(Kind), Parchment, 0.4f))
				]
			];
	}

	TSharedRef<SWidget> CoinBadge(int32 Amount, int32 FontSize)
	{
		return SNew(SBorder)
			.BorderImage(Rounded(Mix(Gold, Card, 0.55f), 12.f, Gold, 1.5f))
			.Padding(FMargin(12.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					// Une pièce : disque doré.
					SNew(SBox).WidthOverride(FontSize + 2.f).HeightOverride(FontSize + 2.f)
					[
						SNew(SBorder).BorderImage(Rounded(Gold, (FontSize + 2.f) * 0.5f, Mix(Gold, Ink, 0.4f), 1.f))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::AsNumber(Amount)).Font(Font(FontSize, true)).ColorAndOpacity(Ink)
				]
			];
	}

	TSharedRef<SWidget> Separator()
	{
		return SNew(SBox).HeightOverride(2.f)
			[
				SNew(SBorder).BorderImage(Rounded(Mix(Parchment, Edge, 0.45f), 1.f))
			];
	}

	TSharedRef<SWidget> CardBox(TSharedRef<SWidget> Content, float Padding)
	{
		return SNew(SBorder)
			.BorderImage(Rounded(Card, 10.f, Mix(Parchment, Edge, 0.5f), 1.f))
			.Padding(Padding)
			[
				Content
			];
	}

	TSharedRef<SWidget> Window(const FText& Title, TSharedRef<SWidget> Content, FSimpleDelegate OnClose, float Width, const FText& Subtitle)
	{
		TSharedRef<SVerticalBox> Heading = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Title).Font(Font(22, true)).ColorAndOpacity(Ink)
			];
		if (!Subtitle.IsEmpty())
		{
			Heading->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(STextBlock).Text(Subtitle).Font(Font(11)).ColorAndOpacity(MutedInk)
			];
		}

		TSharedRef<SWidget> Panel = SNew(SBorder)
			.BorderImage(Rounded(Parchment, 16.f, Edge, 2.f))
			.Padding(FMargin(24.f, 20.f, 24.f, 22.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Heading]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
					[
						SNew(SBox).WidthOverride(34.f).HeightOverride(34.f)
						[
							SNew(SButton)
							.ButtonStyle(&ButtonStyle(EButton::Secondary))
							.ContentPadding(0.f)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.ToolTipText(LOCTEXT("CloseTip", "Fermer (Échap)"))
							.OnClicked_Lambda([OnClose]() { OnClose.ExecuteIfBound(); return FReply::Handled(); })
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("×"))).Font(Font(16, true)).ColorAndOpacity(Ink)
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 14)[Separator()]
				+ SVerticalBox::Slot().AutoHeight()[Content]
			];

		return SNew(SOverlay)
			// Le jeu reste visible, flouté et assombri, derrière la fenêtre.
			+ SOverlay::Slot()
			[
				SNew(SBackgroundBlur).BlurStrength(5.f)
				[
					SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.04f, 0.03f, 0.02f, 0.35f))
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(Width)
				[
					SNew(SOverlay)
					// Ombre portée douce, légèrement décalée.
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderImage(Rounded(FLinearColor(0.f, 0.f, 0.f, 0.28f), 18.f))
						.RenderTransform(FSlateRenderTransform(FVector2D(5.f, 8.f)))
					]
					+ SOverlay::Slot()[Panel]
				]
			];
	}

	TSharedRef<SWidget> ItemIcon(const FItemDef* Def, float Size)
	{
		if (!Def)
		{
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size);
		}

		// Les textures chargées restent vivantes pour toute la session (catalogue court).
		static TMap<FName, TSharedPtr<FSlateBrush>> Brushes;
		static TArray<TStrongObjectPtr<UTexture2D>> Textures;
		TSharedPtr<FSlateBrush>* Cached = Brushes.Find(Def->Id);
		if (!Cached)
		{
			TSharedPtr<FSlateBrush> Brush;
			if (UTexture2D* Texture = Cast<UTexture2D>(Def->Icon.TryLoad()))
			{
				Textures.Emplace(Texture);
				Brush = MakeShared<FSlateBrush>();
				Brush->SetResourceObject(Texture);
				Brush->ImageSize = FVector2D(128.f);
				// Coins arrondis : l'icône s'intègre au style des cartes.
				Brush->DrawAs = ESlateBrushDrawType::RoundedBox;
				Brush->OutlineSettings = FSlateBrushOutlineSettings(FVector4(10.f, 10.f, 10.f, 10.f));
			}
			Cached = &Brushes.Add(Def->Id, Brush);
		}

		if (Cached->IsValid())
		{
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size)[SNew(SImage).Image(Cached->Get())];
		}
		return SNew(SBox).WidthOverride(Size * 0.6f).HeightOverride(Size * 0.6f)
			[
				SNew(SBorder).BorderImage(Rounded(Def->Color, Size * 0.3f))
			];
	}

	TSharedRef<SToolTip> ItemTooltip(const FItemDef& Def)
	{
		return SNew(SToolTip)
			.BorderImage(Rounded(Card, 8.f, Edge, 1.5f))
			[
				SNew(SBox).MaxDesiredWidth(280.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Def.Name).Font(Font(12, true)).ColorAndOpacity(Ink)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[
						SNew(STextBlock).Text(Def.Description).Font(Font(10)).ColorAndOpacity(MutedInk).AutoWrapText(true)
					]
				]
			];
	}
}

#undef LOCTEXT_NAMESPACE
