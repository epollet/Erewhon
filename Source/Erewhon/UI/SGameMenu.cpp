#include "UI/SGameMenu.h"

#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/ErewhonSlate.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ErewhonGameMenu"

using namespace ErewhonUI;

namespace
{
	constexpr float GMainWidth = 420.f;
	constexpr float GOptionsWidth = 640.f;

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
		return bValue ? LOCTEXT("On", "Activé") : LOCTEXT("Off", "Désactivé");
	}

	const TArray<FText>& QualityNames()
	{
		static const TArray<FText> Names = {
			LOCTEXT("Low", "Faible"), LOCTEXT("Medium", "Moyenne"), LOCTEXT("High", "Haute"),
			LOCTEXT("Epic", "Épique"), LOCTEXT("Cinematic", "Cinématique"),
		};
		return Names;
	}
}

void SGameMenu::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	ChildSlot[SAssignNew(Body, SBox)];
	Show(EPage::Main);
}

FReply SGameMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		// Depuis les options, Échap revient au menu ; depuis le menu, il le ferme.
		if (Page != EPage::Main)
		{
			Show(EPage::Main);
		}
		else if (Controller.IsValid())
		{
			Controller->CloseMenu();
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SGameMenu::Show(EPage InPage)
{
	Page = InPage;
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	const FSimpleDelegate Close = FSimpleDelegate::CreateLambda([WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } });
	if (Page == EPage::Main)
	{
		Body->SetContent(Window(LOCTEXT("Title", "Erewhon"), BuildMain(), Close, GMainWidth, LOCTEXT("Subtitle", "Une pause dans la prairie")));
	}
	else
	{
		Body->SetContent(Window(LOCTEXT("OptionsTitle", "Options"), BuildOptions(), Close, GOptionsWidth,
			LOCTEXT("OptionsSubtitle", "Les réglages sont enregistrés aussitôt")));
	}
}

TSharedRef<SWidget> SGameMenu::BuildMain()
{
	TWeakObjectPtr<AErewhonPlayerController> WeakController = Controller;
	const auto Entry = [](TSharedRef<SWidget> Widget) { return SNew(SBox).HeightOverride(46.f)[Widget]; };
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			Entry(Button(LOCTEXT("Resume", "Reprendre"), [WeakController]() { if (WeakController.IsValid()) { WeakController->CloseMenu(); } }, EButton::Primary, true, 0.f, 14))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			Entry(Button(LOCTEXT("Options", "Options"), [this]() { Show(EPage::Display); }, EButton::Secondary, true, 0.f, 14))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			Entry(Button(LOCTEXT("Quit", "Quitter le jeu"), [WeakController]()
			{
				if (WeakController.IsValid())
				{
					UKismetSystemLibrary::QuitGame(WeakController.Get(), WeakController.Get(), EQuitPreference::Quit, false);
				}
			}, EButton::Danger, true, 0.f, 14))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0).HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Controls", "ZQSD : marcher · Maj : courir · Ctrl : s'accroupir\nClic : récolter · E : interagir · I : inventaire"))
			.Justification(ETextJustify::Center)
			.Font(Font(9)).ColorAndOpacity(MutedInk)
		];
}

TSharedRef<SWidget> SGameMenu::OptionRow(const FText& Label, const FText& Help, TFunction<FText()> Value, TFunction<void(int32)> Step)
{
	const auto Arrow = [Step](const TCHAR* Glyph, int32 Direction)
	{
		return SNew(SBox).WidthOverride(32.f).HeightOverride(30.f)
			[
				SNew(SButton)
				.ButtonStyle(&ButtonStyle(EButton::Secondary))
				.ContentPadding(0.f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked_Lambda([Step, Direction]() { Step(Direction); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(FText::FromString(Glyph)).Font(Font(13, true)).ColorAndOpacity(Ink)
				]
			];
	};
	return CardBox(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Label).Font(Font(11, true)).ColorAndOpacity(Ink)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)[SNew(STextBlock).Text(Help).Font(Font(9)).ColorAndOpacity(MutedInk)]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Arrow(TEXT("‹"), -1)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
		[
			SNew(SBox).WidthOverride(150.f).HeightOverride(30.f)
			[
				SNew(SBorder)
				.BorderImage(Rounded(Mix(Gold, Card, 0.7f), 12.f, Mix(Gold, Edge, 0.5f), 1.f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text_Lambda([Value]() { return Value(); }).Font(Font(11, true)).ColorAndOpacity(Ink)
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Arrow(TEXT("›"), 1)],
		10.f);
}

TSharedRef<SWidget> SGameMenu::BuildOptions()
{
	UErewhonUserSettings* Settings = UErewhonUserSettings::Get();
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	if (!Settings)
	{
		return Rows;
	}
	TWeakObjectPtr<UErewhonUserSettings> Weak = Settings;

	// Réglages du moteur : appliqués et sauvegardés aussitôt.
	const auto ApplyEngine = [Weak]() { if (Weak.IsValid()) { Weak->ApplySettings(false); } };
	// Réglages du monde : sauvegardés, puis les secteurs se reconstruisent.
	const auto ApplyWorld = [Weak]() { if (Weak.IsValid()) { Weak->NotifyWorldDetailChanged(); } };

	static const TArray<int32> FrameLimits = { 0, 30, 60, 120, 144 };
	static const TArray<float> Distances = { 0.5f, 0.75f, 1.f, 1.25f, 1.5f };
	static const TArray<EWindowMode::Type> WindowModes = { EWindowMode::WindowedFullscreen, EWindowMode::Windowed, EWindowMode::Fullscreen };

	// Onglets.
	const auto Tab = [this](const FText& Label, EPage Target)
	{
		return Button(Label, [this, Target]() { Show(Target); }, Page == Target ? EButton::TabActive : EButton::Tab, true, 120.f, 11);
	};
	Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Tab(LOCTEXT("TabDisplay", "Affichage"), EPage::Display)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Tab(LOCTEXT("TabWorld", "Monde"), EPage::World)]
		+ SHorizontalBox::Slot().AutoWidth()[Tab(LOCTEXT("TabSound", "Son"), EPage::Sound)]
	];
	const auto Add = [&Rows](TSharedRef<SWidget> Row) { Rows->AddSlot().AutoHeight().Padding(0, 4)[Row]; };

	if (Page == EPage::Display)
	{
		Add(OptionRow(LOCTEXT("Quality", "Qualité globale"), LOCTEXT("QualityHelp", "Le réglage qui pèse le plus sur les performances."),
			[Weak]() { return Weak.IsValid() ? QualityNames()[FMath::Clamp(Weak->GetOverallScalabilityLevel(), 0, 4)] : FText::GetEmpty(); },
			[Weak, ApplyEngine](int32 Dir)
			{
				const int32 Level = Weak->GetOverallScalabilityLevel();
				Weak->SetOverallScalabilityLevel(FMath::Clamp((Level < 0 ? 2 : Level) + Dir, 0, 4));
				ApplyEngine();
			}));
		Add(OptionRow(LOCTEXT("Resolution", "Résolution 3D"), LOCTEXT("ResolutionHelp", "Rendu interne, remis à l'échelle de l'écran."),
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
		Add(OptionRow(LOCTEXT("WindowMode", "Mode d'écran"), LOCTEXT("WindowModeHelp", "Fenêtré, plein écran fenêtré ou exclusif."),
			[Weak]()
			{
				switch (Weak->GetFullscreenMode())
				{
				case EWindowMode::Fullscreen: return LOCTEXT("Fullscreen", "Plein écran");
				case EWindowMode::Windowed: return LOCTEXT("Windowed", "Fenêtré");
				default: return LOCTEXT("Borderless", "Sans bordure");
				}
			},
			[Weak, ApplyEngine](int32 Dir) { Weak->SetFullscreenMode(Cycle(WindowModes, Weak->GetFullscreenMode(), Dir)); ApplyEngine(); }));
		Add(OptionRow(LOCTEXT("VSync", "Synchronisation verticale"), LOCTEXT("VSyncHelp", "Évite les déchirures d'image."),
			[Weak]() { return OnOff(Weak->IsVSyncEnabled()); },
			[Weak, ApplyEngine](int32) { Weak->SetVSyncEnabled(!Weak->IsVSyncEnabled()); ApplyEngine(); }));
		Add(OptionRow(LOCTEXT("FrameLimit", "Limite d'images"), LOCTEXT("FrameLimitHelp", "Plafonne les images par seconde."),
			[Weak]()
			{
				const int32 Limit = FMath::RoundToInt32(Weak->GetFrameRateLimit());
				return Limit <= 0 ? LOCTEXT("Unlimited", "Illimitée") : FText::Format(LOCTEXT("FpsFmt", "{0} i/s"), FText::AsNumber(Limit));
			},
			[Weak, ApplyEngine](int32 Dir) { Weak->SetFrameRateLimit(Cycle(FrameLimits, FMath::RoundToInt32(Weak->GetFrameRateLimit()), Dir)); ApplyEngine(); }));
		Add(OptionRow(LOCTEXT("Clouds", "Qualité des nuages"), LOCTEXT("CloudsHelp", "Les nuages volumétriques coûtent cher."),
			[Weak]() { return QualityNames()[FMath::Clamp(Weak->CloudQuality, 0, 2)]; },
			[Weak, ApplyWorld](int32 Dir) { Weak->CloudQuality = FMath::Clamp(Weak->CloudQuality + Dir, 0, 2); ApplyWorld(); }));
		Add(OptionRow(LOCTEXT("ShowFps", "Compteur d'images"), LOCTEXT("ShowFpsHelp", "En haut à droite de l'écran."),
			[Weak]() { return OnOff(Weak->bShowFps); },
			[Weak](int32) { Weak->bShowFps = !Weak->bShowFps; Weak->SaveSettings(); }));
	}
	else if (Page == EPage::Sound)
	{
		const auto Volume = [Weak](float UErewhonUserSettings::* Field)
		{
			return [Weak, Field]() { return Percent(Weak.IsValid() ? Weak.Get()->*Field : 0.f); };
		};
		const auto StepVolume = [Weak](float UErewhonUserSettings::* Field)
		{
			return [Weak, Field](int32 Dir)
			{
				if (Weak.IsValid())
				{
					float& Value = Weak.Get()->*Field;
					Value = FMath::Clamp(FMath::RoundToFloat(Value * 10.f + Dir) / 10.f, 0.f, 1.f);
					Weak->SaveSettings();
				}
			};
		};
		Add(OptionRow(LOCTEXT("Effects", "Bruitages"), LOCTEXT("EffectsHelp", "Pas, porte, oiseaux, bruissement de l'herbe."),
			Volume(&UErewhonUserSettings::EffectsVolume), StepVolume(&UErewhonUserSettings::EffectsVolume)));
		Add(OptionRow(LOCTEXT("Ambient", "Ambiance"), LOCTEXT("AmbientHelp", "Le vent dans la prairie."),
			Volume(&UErewhonUserSettings::AmbientVolume), StepVolume(&UErewhonUserSettings::AmbientVolume)));
	}
	else
	{
		Add(OptionRow(LOCTEXT("ViewDistance", "Distance de vue"), LOCTEXT("ViewDistanceHelp", "Secteurs affichés autour de vous."),
			[Weak]() { return FText::Format(LOCTEXT("SectorsFmt", "{0} secteur(s)"), FText::AsNumber(Weak->ViewDistanceSectors)); },
			[Weak, ApplyWorld](int32 Dir) { Weak->ViewDistanceSectors = FMath::Clamp(Weak->ViewDistanceSectors + Dir, 1, 3); ApplyWorld(); }));
		Add(OptionRow(LOCTEXT("VegDistance", "Distance de la végétation"), LOCTEXT("VegDistanceHelp", "Jusqu'où l'herbe, les fleurs et les fougères s'affichent."),
			[Weak]() { return Percent(Weak->VegetationDistance); },
			[Weak, ApplyWorld](int32 Dir) { Weak->VegetationDistance = Cycle(Distances, Weak->VegetationDistance, Dir); ApplyWorld(); }));
		Add(OptionRow(LOCTEXT("VegShadows", "Ombres de la végétation"), LOCTEXT("VegShadowsHelp", "Plus joli, plus coûteux."),
			[Weak]() { return OnOff(Weak->bVegetationShadows); },
			[Weak, ApplyWorld](int32) { Weak->bVegetationShadows = !Weak->bVegetationShadows; ApplyWorld(); }));
		Rows->AddSlot().AutoHeight().Padding(2, 8, 0, 0)
		[
			SNew(STextBlock).Text(LOCTEXT("WorldHint", "Ces réglages reconstruisent les secteurs autour de vous."))
			.Font(Font(9)).ColorAndOpacity(MutedInk)
		];
	}

	Rows->AddSlot().AutoHeight().Padding(0, 14, 0, 0).HAlign(HAlign_Left)
	[
		Button(LOCTEXT("Back", "‹  Retour"), [this]() { Show(EPage::Main); }, EButton::Secondary, true, 120.f)
	];
	return Rows;
}

#undef LOCTEXT_NAMESPACE
