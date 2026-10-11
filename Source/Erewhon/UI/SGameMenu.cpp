#include "UI/SGameMenu.h"

#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ErewhonGameMenu"

using namespace ErewhonUI;

namespace
{
	constexpr float GMenuWidth = 520.f;

	TSharedRef<SWidget> MenuButton(const FText& Label, TFunction<void()> OnClick)
	{
		return SNew(SBox).HeightOverride(44.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(Label).Font(Font(14, true))
				]
			];
	}

	/** Valeur suivante ou précédente dans une liste, en bouclant. */
	template <typename T>
	T Cycle(const TArray<T>& Values, const T& Current, int32 Direction)
	{
		int32 Index = Values.IndexOfByKey(Current);
		Index = Index == INDEX_NONE ? 0 : (Index + Direction + Values.Num()) % Values.Num();
		return Values[Index];
	}

	FText Percent(float Value)
	{
		return FText::Format(LOCTEXT("PercentFmt", "{0} %"), FText::AsNumber(FMath::RoundToInt32(Value * 100.f)));
	}

	FText OnOff(bool bValue)
	{
		return bValue ? LOCTEXT("On", "Oui") : LOCTEXT("Off", "Non");
	}
}

void SGameMenu::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	ChildSlot
	[
		Window(LOCTEXT("Title", "Menu"), SAssignNew(Body, SBox),
			FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }),
			GMenuWidth)
	];
	ShowMain();
}

FReply SGameMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		// Depuis les options, Échap revient au menu ; depuis le menu, il le ferme.
		if (bShowingOptions)
		{
			ShowMain();
		}
		else if (Controller.IsValid())
		{
			Controller->CloseMenu();
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SGameMenu::ShowMain()
{
	bShowingOptions = false;
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			MenuButton(LOCTEXT("Resume", "Reprendre"), [WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			MenuButton(LOCTEXT("Graphics", "Options graphiques"), [this]() { ShowOptions(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			MenuButton(LOCTEXT("Quit", "Quitter le jeu"), [WeakController]()
			{
				if (WeakController.IsValid())
				{
					UKismetSystemLibrary::QuitGame(WeakController.Get(), WeakController.Get(), EQuitPreference::Quit, false);
				}
			})
		]);
}

TSharedRef<SWidget> SGameMenu::OptionRow(const FText& Label, TFunction<FText()> Value, TFunction<void(int32)> Step)
{
	const auto Arrow = [Step](const FText& Glyph, int32 Direction)
	{
		return SNew(SBox).WidthOverride(36.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([Step, Direction]() { Step(Direction); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(Glyph).Font(Font(12, true))
				]
			];
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Label).Font(Font(11)).ColorAndOpacity(TextColor)
		]
		+ SHorizontalBox::Slot().AutoWidth()[Arrow(LOCTEXT("Prev", "<"), -1)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(170.f).HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([Value]() { return Value(); }).Font(Font(11, true)).ColorAndOpacity(CoinColor)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()[Arrow(LOCTEXT("Next", ">"), 1)];
}

void SGameMenu::ShowOptions()
{
	bShowingOptions = true;
	UErewhonUserSettings* Settings = UErewhonUserSettings::Get();
	if (!Settings)
	{
		return;
	}
	TWeakObjectPtr<UErewhonUserSettings> Weak = Settings;

	// Réglages du moteur : appliqués et sauvegardés aussitôt.
	const auto ApplyEngine = [Weak]()
	{
		if (Weak.IsValid())
		{
			Weak->ApplySettings(false);
		}
	};
	// Réglages du monde : sauvegardés, puis les secteurs se reconstruisent.
	const auto ApplyWorld = [Weak]()
	{
		if (Weak.IsValid())
		{
			Weak->NotifyWorldDetailChanged();
		}
	};

	static const TArray<FText> QualityNames = {
		LOCTEXT("Low", "Faible"), LOCTEXT("Medium", "Moyenne"), LOCTEXT("High", "Haute"),
		LOCTEXT("Epic", "Épique"), LOCTEXT("Cinematic", "Cinématique"),
	};
	static const TArray<int32> FrameLimits = { 0, 30, 60, 120, 144 };
	static const TArray<float> Densities = { 0.f, 0.25f, 0.5f, 0.75f, 1.f, 1.5f };
	static const TArray<float> Distances = { 0.5f, 0.75f, 1.f, 1.25f, 1.5f };
	static const TArray<EWindowMode::Type> WindowModes = { EWindowMode::WindowedFullscreen, EWindowMode::Windowed, EWindowMode::Fullscreen };

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](TSharedRef<SWidget> Row) { Rows->AddSlot().AutoHeight().Padding(0, 3)[Row]; };
	const auto Section = [&Rows](const FText& Title)
	{
		Rows->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[SNew(STextBlock).Text(Title).Font(Font(12, true)).ColorAndOpacity(MutedText)];
	};

	Section(LOCTEXT("Display", "Affichage"));
	Add(OptionRow(LOCTEXT("Quality", "Qualité globale"),
		[Weak]() { return Weak.IsValid() ? QualityNames[FMath::Clamp(Weak->GetOverallScalabilityLevel(), 0, 4)] : FText::GetEmpty(); },
		[Weak, ApplyEngine](int32 Dir)
		{
			const int32 Level = Weak->GetOverallScalabilityLevel();
			Weak->SetOverallScalabilityLevel(FMath::Clamp((Level < 0 ? 2 : Level) + Dir, 0, 4));
			ApplyEngine();
		}));
	Add(OptionRow(LOCTEXT("Resolution", "Résolution 3D"),
		[Weak]()
		{
			float Normalized, Value, Min, Max;
			Weak->GetResolutionScaleInformationEx(Normalized, Value, Min, Max);
			return FText::Format(LOCTEXT("ResFmt", "{0} %"), FText::AsNumber(FMath::RoundToInt32(Value)));
		},
		[Weak, ApplyEngine](int32 Dir)
		{
			float Normalized, Value, Min, Max;
			Weak->GetResolutionScaleInformationEx(Normalized, Value, Min, Max);
			Weak->SetResolutionScaleValueEx(FMath::Clamp(FMath::RoundToFloat(Value / 10.f) * 10.f + Dir * 10.f, FMath::Max(Min, 30.f), Max));
			ApplyEngine();
		}));
	Add(OptionRow(LOCTEXT("WindowMode", "Mode d'écran"),
		[Weak]()
		{
			switch (Weak->GetFullscreenMode())
			{
			case EWindowMode::Fullscreen: return LOCTEXT("Fullscreen", "Plein écran");
			case EWindowMode::Windowed: return LOCTEXT("Windowed", "Fenêtré");
			default: return LOCTEXT("Borderless", "Plein écran fenêtré");
			}
		},
		[Weak, ApplyEngine](int32 Dir) { Weak->SetFullscreenMode(Cycle(WindowModes, Weak->GetFullscreenMode(), Dir)); ApplyEngine(); }));
	Add(OptionRow(LOCTEXT("VSync", "Synchronisation verticale"),
		[Weak]() { return OnOff(Weak->IsVSyncEnabled()); },
		[Weak, ApplyEngine](int32) { Weak->SetVSyncEnabled(!Weak->IsVSyncEnabled()); ApplyEngine(); }));
	Add(OptionRow(LOCTEXT("FrameLimit", "Limite d'images"),
		[Weak]()
		{
			const int32 Limit = FMath::RoundToInt32(Weak->GetFrameRateLimit());
			return Limit <= 0 ? LOCTEXT("Unlimited", "Illimitée") : FText::Format(LOCTEXT("FpsFmt", "{0} i/s"), FText::AsNumber(Limit));
		},
		[Weak, ApplyEngine](int32 Dir) { Weak->SetFrameRateLimit(Cycle(FrameLimits, FMath::RoundToInt32(Weak->GetFrameRateLimit()), Dir)); ApplyEngine(); }));
	Add(OptionRow(LOCTEXT("ShowFps", "Afficher les images/s"),
		[Weak]() { return OnOff(Weak->bShowFps); },
		[Weak](int32) { Weak->bShowFps = !Weak->bShowFps; Weak->SaveSettings(); }));

	Add(OptionRow(LOCTEXT("Clouds", "Qualité des nuages"),
		[Weak]() { return QualityNames[FMath::Clamp(Weak->CloudQuality, 0, 2)]; },
		[Weak, ApplyWorld](int32 Dir) { Weak->CloudQuality = FMath::Clamp(Weak->CloudQuality + Dir, 0, 2); ApplyWorld(); }));

	Section(LOCTEXT("World", "Monde"));
	Add(OptionRow(LOCTEXT("ViewDistance", "Distance de vue"),
		[Weak]() { return FText::Format(LOCTEXT("SectorsFmt", "{0} secteur(s)"), FText::AsNumber(Weak->ViewDistanceSectors)); },
		[Weak, ApplyWorld](int32 Dir) { Weak->ViewDistanceSectors = FMath::Clamp(Weak->ViewDistanceSectors + Dir, 1, 3); ApplyWorld(); }));
	Add(OptionRow(LOCTEXT("Density", "Plantes décoratives"),
		[Weak]() { return Percent(Weak->VegetationDensity); },
		[Weak, ApplyWorld](int32 Dir) { Weak->VegetationDensity = Cycle(Densities, Weak->VegetationDensity, Dir); ApplyWorld(); }));
	Add(OptionRow(LOCTEXT("VegDistance", "Distance de la végétation"),
		[Weak]() { return Percent(Weak->VegetationDistance); },
		[Weak, ApplyWorld](int32 Dir) { Weak->VegetationDistance = Cycle(Distances, Weak->VegetationDistance, Dir); ApplyWorld(); }));
	Add(OptionRow(LOCTEXT("VegShadows", "Ombres de la végétation"),
		[Weak]() { return OnOff(Weak->bVegetationShadows); },
		[Weak, ApplyWorld](int32) { Weak->bVegetationShadows = !Weak->bVegetationShadows; ApplyWorld(); }));

	Rows->AddSlot().AutoHeight().Padding(0, 14, 0, 0)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("Hint", "Les réglages du monde reconstruisent les secteurs autour de vous."))
		.Font(Font(9)).ColorAndOpacity(MutedText).AutoWrapText(true)
	];
	Rows->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
	[
		MenuButton(LOCTEXT("Back", "Retour"), [this]() { ShowMain(); })
	];
	Body->SetContent(Rows);
}

#undef LOCTEXT_NAMESPACE
