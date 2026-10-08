#include "UI/ErewhonSlate.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "Items/ItemTypes.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ErewhonUI"

namespace ErewhonUI
{
	const FSlateBrush* WhiteBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	FSlateFontInfo Font(int32 Size, bool bBold)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	TSharedRef<SWidget> Separator()
	{
		return SNew(SBox).HeightOverride(1.f)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PanelBorder * FLinearColor(1, 1, 1, 0.5f))
			];
	}

	TSharedRef<SWidget> Window(const FText& Title, TSharedRef<SWidget> Content, FSimpleDelegate OnClose, float Width)
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(Width)
				[
					// Liseré extérieur puis fond du panneau.
					SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PanelBorder).Padding(2.f)
					[
						SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(PanelBackground).Padding(16.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(Title).Font(Font(18, true)).ColorAndOpacity(TextColor)
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
									.OnClicked_Lambda([OnClose]() { OnClose.ExecuteIfBound(); return FReply::Handled(); })
									.ToolTipText(LOCTEXT("CloseTip", "Fermer (Échap)"))
									[
										SNew(STextBlock).Text(LOCTEXT("Close", "X")).Font(Font(12, true))
									]
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								Content
							]
						]
					]
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
			}
			Cached = &Brushes.Add(Def->Id, Brush);
		}

		if (Cached->IsValid())
		{
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size)[SNew(SImage).Image(Cached->Get())];
		}
		return SNew(SBox).WidthOverride(Size * 0.6f).HeightOverride(Size * 0.6f)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Def->Color)
			];
	}

	TSharedRef<SToolTip> ItemTooltip(const FItemDef& Def)
	{
		return SNew(SToolTip)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Def.Name).Font(Font(12, true))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(STextBlock).Text(Def.Description).Font(Font(10)).AutoWrapText(true)
				]
			];
	}
}

#undef LOCTEXT_NAMESPACE
