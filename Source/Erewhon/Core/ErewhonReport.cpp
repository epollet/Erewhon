#include "Core/ErewhonReport.h"

#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonUserSettings.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/ErewhonCharacter.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sky/CelestialSystem.h"
#include "Sky/ErewhonWeather.h"
#include "World/Bhurak.h"
#include "World/SectorTypes.h"
#include "World/Storage.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "ErewhonReport"

DEFINE_LOG_CATEGORY_STATIC(LogErewhonReport, Log, All);

namespace
{
	/** Lignes de fin du log jointes au signalement. */
	constexpr int32 GReportLogLines = 3000;

	const TCHAR* ReportCategoryId(ErewhonReport::ECategory Category)
	{
		switch (Category)
		{
		case ErewhonReport::ECategory::Observation: return TEXT("constat");
		case ErewhonReport::ECategory::Idea: return TEXT("idee");
		default: return TEXT("bug");
		}
	}

	/** Nom de dossier lisible : minuscules ASCII, accents ôtés, tirets entre les mots. */
	FString ReportSlug(const FString& Text)
	{
		static const TCHAR* Accents[][2] = {
			{ TEXT("àâä"), TEXT("a") }, { TEXT("éèêë"), TEXT("e") }, { TEXT("îï"), TEXT("i") },
			{ TEXT("ôö"), TEXT("o") }, { TEXT("ùûü"), TEXT("u") }, { TEXT("ç"), TEXT("c") },
		};
		FString Slug;
		for (TCHAR Char : Text.ToLower())
		{
			for (const auto& Accent : Accents)
			{
				if (FCString::Strchr(Accent[0], Char))
				{
					Char = Accent[1][0];
				}
			}
			if (Char < 128 && FChar::IsAlnum(Char))
			{
				Slug.AppendChar(Char);
			}
			else if (!Slug.IsEmpty() && !Slug.EndsWith(TEXT("-")))
			{
				Slug.AppendChar(TEXT('-'));
			}
			if (Slug.Len() >= 40)
			{
				break;
			}
		}
		Slug.RemoveFromEnd(TEXT("-"));
		return Slug;
	}

	/** Le commit du dépôt, lu dans .git (absent d'un jeu empaqueté). */
	FString ReportGitCommit()
	{
		const FString GitDir = FPaths::ProjectDir() / TEXT(".git");
		FString Head;
		if (!FFileHelper::LoadFileToString(Head, *(GitDir / TEXT("HEAD"))))
		{
			return TEXT("inconnu");
		}
		Head.TrimStartAndEndInline();
		if (!Head.StartsWith(TEXT("ref: ")))
		{
			return Head;
		}
		const FString Ref = Head.RightChop(5);
		FString Hash;
		if (FFileHelper::LoadFileToString(Hash, *(GitDir / Ref)))
		{
			return Ref + TEXT(" ") + Hash.TrimStartAndEnd();
		}
		TArray<FString> Packed;
		FFileHelper::LoadFileToStringArray(Packed, *(GitDir / TEXT("packed-refs")));
		for (const FString& Line : Packed)
		{
			if (Line.EndsWith(TEXT(" ") + Ref))
			{
				return Ref + TEXT(" ") + Line.Left(40);
			}
		}
		return Ref;
	}

	FString ReportNetMode(const UWorld* World)
	{
		switch (World->GetNetMode())
		{
		case NM_Standalone: return TEXT("solo");
		case NM_ListenServer: return TEXT("hôte (listen server)");
		case NM_DedicatedServer: return TEXT("serveur dédié");
		default: return TEXT("client");
		}
	}

	FString ReportSlots(const TArray<FInventorySlot>& Slots)
	{
		TArray<FString> Parts;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (!Slots[Index].ItemId.IsNone() && Slots[Index].Count > 0)
			{
				Parts.Add(FString::Printf(TEXT("[%d] %s ×%d"), Index, *Slots[Index].ItemId.ToString(), Slots[Index].Count));
			}
		}
		return Parts.IsEmpty() ? TEXT("vide") : FString::Join(Parts, TEXT(", "));
	}

	FString ReportActorName(const AActor* Actor)
	{
		return Actor ? FString::Printf(TEXT("%s (%s)"), *Actor->GetName(), *Actor->GetClass()->GetName()) : TEXT("aucun");
	}

	void CaptureScreen(AErewhonPlayerController* Controller, ErewhonReport::FCapture& Out)
	{
		const UGameViewportClient* Viewport = Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->ViewportClient : nullptr;
		const TSharedPtr<SWindow> GameWindow = Viewport ? Viewport->GetWindow() : nullptr;
		FIntVector Size = FIntVector::ZeroValue;
		if (!GameWindow.IsValid() || !FSlateApplication::IsInitialized()
			|| !FSlateApplication::Get().TakeScreenshot(GameWindow.ToSharedRef(), Out.Pixels, Size))
		{
			Out.Pixels.Reset();
			return;
		}
		Out.Size = FIntPoint(Size.X, Size.Y);
		for (FColor& Pixel : Out.Pixels)
		{
			Pixel.A = 255;   // l'alpha laissé par Slate n'est pas une opacité
		}
	}

	TArray<ErewhonReport::FSection> CaptureContext(AErewhonPlayerController* Controller, const FDateTime& Time)
	{
		TArray<ErewhonReport::FSection> Sections;
		UWorld* World = Controller->GetWorld();
		const AErewhonGameState* GameState = AErewhonGameState::Get(Controller);
		const AErewhonPlayerState* PlayerState = Controller->GetPlayerState<AErewhonPlayerState>();
		APawn* Pawn = Controller->GetPawn();

		ErewhonReport::FSection& Build = Sections.AddDefaulted_GetRef();
		Build.Name = TEXT("Version");
		Build.Add(TEXT("Date"), Time.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
		Build.Add(TEXT("Commit"), ReportGitCommit());
		Build.Add(TEXT("Configuration"), LexToString(FApp::GetBuildConfiguration()));
		Build.Add(TEXT("Moteur"), FEngineVersion::Current().ToString());
		Build.Add(TEXT("Ligne de commande"), FCommandLine::Get());

		ErewhonReport::FSection& Machine = Sections.AddDefaulted_GetRef();
		Machine.Name = TEXT("Machine");
		Machine.Add(TEXT("OS"), FPlatformMisc::GetOSVersion());
		Machine.Add(TEXT("CPU"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
		Machine.Add(TEXT("GPU"), GRHIAdapterName);
		Machine.Add(TEXT("RHI"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("?"));
		const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
		Machine.Add(TEXT("Mémoire"), FString::Printf(TEXT("%llu Mo utilisés / %llu Mo"),
			Memory.UsedPhysical / (1024 * 1024), Memory.TotalPhysical / (1024 * 1024)));
		if (const UGameViewportClient* Viewport = Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->ViewportClient : nullptr)
		{
			FVector2D ViewportSize;
			Viewport->GetViewportSize(ViewportSize);
			Machine.Add(TEXT("Résolution"), FString::Printf(TEXT("%.0f × %.0f"), ViewportSize.X, ViewportSize.Y));
		}
		if (UErewhonUserSettings* User = UErewhonUserSettings::Get())
		{
			Machine.Add(TEXT("Qualité"), FString::FromInt(User->GetOverallScalabilityLevel()));
		}
		const double FrameMs = FApp::GetDeltaTime() * 1000.0;
		Machine.Add(TEXT("Image"), FString::Printf(TEXT("%.1f ms (%.0f i/s)"), FrameMs, FrameMs > 0.0 ? 1000.0 / FrameMs : 0.0));

		ErewhonReport::FSection& Game = Sections.AddDefaulted_GetRef();
		Game.Name = TEXT("Partie");
		Game.Add(TEXT("Carte"), World->GetMapName());
		Game.Add(TEXT("Réseau"), ReportNetMode(World));
		if (GameState)
		{
			const double Clock = GameState->GetWorldClock();
			Game.Add(TEXT("Joueurs"), FString::FromInt(GameState->PlayerArray.Num()));
			Game.Add(TEXT("Seed"), FString::FromInt(GameState->GetWorldSeed()));
			Game.Add(TEXT("Horloge du monde"), FString::Printf(TEXT("%.1f s"), Clock));
			if (const ACelestialSystem* Celestial = ACelestialSystem::Find(Controller))
			{
				Game.Add(TEXT("Heure"), FString::Printf(TEXT("Soleil %s, Usha %s"),
					*ACelestialSystem::ClockString(Celestial->GetSunOrbit(), Clock), *ACelestialSystem::ClockString(Celestial->GetUshaOrbit(), Clock)));
			}
			const ErewhonWeather::FState Weather = GameState->SampleWeather();
			Game.Add(TEXT("Météo"), FString::Printf(TEXT("%s%s (temps %.2f, pluie %.2f, vent ×%.2f)"),
				*ErewhonWeather::KindName(Weather.Kind).ToString(), GameState->IsWeatherForced() ? TEXT(", forcée par la triche") : TEXT(""),
				Weather.Cloudiness, Weather.Rain, Weather.Wind));
			TArray<FString> Upgrades;
			for (int32 Upgrade = 0; Upgrade < static_cast<int32>(EVillageUpgrade::Count); ++Upgrade)
			{
				if (GameState->GetVillageUpgrades() & (1 << Upgrade))
				{
					Upgrades.Add(StaticEnum<EVillageUpgrade>()->GetNameStringByValue(Upgrade));
				}
			}
			Game.Add(TEXT("Village"), Upgrades.IsEmpty() ? TEXT("aucune amélioration") : FString::Join(Upgrades, TEXT(", ")));
		}

		ErewhonReport::FSection& Player = Sections.AddDefaulted_GetRef();
		Player.Name = TEXT("Joueur");
		if (PlayerState)
		{
			Player.Add(TEXT("Nom"), PlayerState->GetPlayerName());
			Player.Add(TEXT("Clé"), PlayerState->GetPlayerKey());
			Player.Add(TEXT("Classe"), StaticEnum<EPlayerClass>()->GetNameStringByValue(static_cast<int64>(PlayerState->GetPlayerClass())));
			Player.Add(TEXT("Pièces"), FString::FromInt(PlayerState->GetCoins()));
		}
		Player.Add(TEXT("Menu ouvert"), Controller->DescribeOpenMenu());
		if (Pawn)
		{
			const FVector Location = Pawn->GetActorLocation();
			const FIntPoint Sector = ErewhonSector::FromWorld(Location);
			Player.Add(TEXT("Pawn"), ReportActorName(Pawn));
			Player.Add(TEXT("Position"), Location.ToString());
			Player.Add(TEXT("Rotation"), Pawn->GetActorRotation().ToString());
			Player.Add(TEXT("Vitesse"), FString::Printf(TEXT("%.0f cm/s"), Pawn->GetVelocity().Size()));
			FString SectorText = ErewhonSector::ToString(Sector);
			if (GameState)
			{
				SectorText += FString::Printf(TEXT(" (%s, propriétaire : %s)"),
					*ErewhonSector::TypeLabel(ErewhonSector::TypeAt(Sector, GameState->GetWorldSeed())).ToString(),
					*GameState->GetSectorOwnerName(Sector));
			}
			Player.Add(TEXT("Secteur"), SectorText);
		}
		if (const ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			Player.Add(TEXT("Mouvement"), Character->GetCharacterMovement()->GetMovementName());
		}
		if (const AErewhonCharacter* Character = Cast<AErewhonCharacter>(Pawn))
		{
			Player.Add(TEXT("Assis"), Character->IsSeated() ? TEXT("oui") : TEXT("non"));
			Player.Add(TEXT("Porte"), ReportActorName(Character->GetCarried()));
			Player.Add(TEXT("Monture"), ReportActorName(Character->GetMount()));
			Player.Add(TEXT("Charrette"), ReportActorName(Character->GetDrivenCart()));
		}

		ErewhonReport::FSection& Aim = Sections.AddDefaulted_GetRef();
		Aim.Name = TEXT("Visée");
		FVector ViewLocation;
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		Aim.Add(TEXT("Caméra"), ViewLocation.ToString());
		Aim.Add(TEXT("Direction"), ViewRotation.ToString());
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonReport), true, Pawn);
		if (World->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 5000.f, ECC_Visibility, Params))
		{
			Aim.Add(TEXT("Sous le regard"), FString::Printf(TEXT("%s, composant %s, à %.0f cm"),
				*ReportActorName(Hit.GetActor()), Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("?"), Hit.Distance));
			Aim.Add(TEXT("Point visé"), Hit.ImpactPoint.ToString());
		}
		if (const AErewhonCharacter* Character = Cast<AErewhonCharacter>(Pawn))
		{
			const FCharacterTarget& Target = Character->GetTarget();
			Aim.Add(TEXT("Interactif"), ReportActorName(Target.Interactable.Get()));
			if (!Target.Hint.IsEmpty())
			{
				Aim.Add(TEXT("Indication"), Target.Hint.ToString());
			}
			if (Target.Plant.IsValid())
			{
				Aim.Add(TEXT("Plante"), FString::Printf(TEXT("%s n°%d du secteur %s"),
					*Target.Plant.ItemId.ToString(), Target.Plant.PlantIndex, *ErewhonSector::ToString(Target.Plant.Sector)));
			}
			if (Target.FieldCell != INDEX_NONE)
			{
				Aim.Add(TEXT("Champ"), FString::Printf(TEXT("case %d du secteur %s"), Target.FieldCell, *ErewhonSector::ToString(Target.FieldSector)));
			}
			if (Target.TreeIndex != INDEX_NONE)
			{
				Aim.Add(TEXT("Arbre"), FString::Printf(TEXT("n°%d du secteur %s"), Target.TreeIndex, *ErewhonSector::ToString(Target.TreeSector)));
			}
		}

		ErewhonReport::FSection& Items = Sections.AddDefaulted_GetRef();
		Items.Name = TEXT("Inventaire");
		if (const UInventoryComponent* Inventory = PlayerState ? PlayerState->GetInventory() : nullptr)
		{
			Items.Add(TEXT("En main"), FString::Printf(TEXT("emplacement %d"), Inventory->HandSlotIndex()));
			Items.Add(TEXT("Poches"), ReportSlots(Inventory->GetSlots()));
		}
		if (const UBackpackComponent* Backpack = PlayerState ? PlayerState->GetBackpack() : nullptr)
		{
			Items.Add(TEXT("Sac à dos"), !Backpack->IsOwned() ? TEXT("aucun")
				: FString::Printf(TEXT("%s : %s"), Backpack->IsWorn() ? TEXT("porté") : TEXT("posé"), *ReportSlots(Backpack->GetSlots())));
		}
		return Sections;
	}

	FString ReportMarkdown(const ErewhonReport::FCapture& Capture, ErewhonReport::ECategory Category,
		const FString& Title, const FString& Description, bool bWithScreenshot)
	{
		FString Text = FString::Printf(TEXT("# [%s] %s\n\n"), *ErewhonReport::CategoryLabel(Category).ToString(), *Title);
		Text += FString::Printf(TEXT("*%s*"), *Capture.Time.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
		Text += bWithScreenshot ? TEXT(" — ![capture](screenshot.png)\n\n") : TEXT("\n\n");
		Text += TEXT("## Description\n\n");
		Text += Description.IsEmpty() ? TEXT("*(aucune)*") : Description;
		Text += TEXT("\n\n## Contexte\n");
		for (const ErewhonReport::FSection& Section : Capture.Context)
		{
			Text += FString::Printf(TEXT("\n### %s\n\n"), *Section.Name);
			for (const TPair<FString, FString>& Value : Section.Values)
			{
				Text += FString::Printf(TEXT("- **%s** : %s\n"), *Value.Key, *Value.Value);
			}
		}
		Text += TEXT("\nLa fin du log du jeu est jointe dans log.txt.\n");
		return Text;
	}

	FString ReportJson(const ErewhonReport::FCapture& Capture, ErewhonReport::ECategory Category,
		const FString& Title, const FString& Description, bool bWithScreenshot)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("category"), ReportCategoryId(Category));
		Root->SetStringField(TEXT("title"), Title);
		Root->SetStringField(TEXT("description"), Description);
		Root->SetStringField(TEXT("time"), Capture.Time.ToIso8601());
		Root->SetBoolField(TEXT("screenshot"), bWithScreenshot);
		const TSharedRef<FJsonObject> Context = MakeShared<FJsonObject>();
		for (const ErewhonReport::FSection& Section : Capture.Context)
		{
			const TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
			for (const TPair<FString, FString>& Value : Section.Values)
			{
				Values->SetStringField(Value.Key, Value.Value);
			}
			Context->SetObjectField(Section.Name, Values);
		}
		Root->SetObjectField(TEXT("context"), Context);
		FString Out;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
		return Out;
	}

	/** La fin du log en cours : le fichier reste ouvert en écriture par le moteur. */
	bool SaveLogTail(const FString& Path)
	{
		GLog->Flush();
		FString Log;
		if (!FFileHelper::LoadFileToString(Log, *FGenericPlatformOutputDevices::GetAbsoluteLogFilename(), FFileHelper::EHashOptions::None, FILEREAD_AllowWrite))
		{
			return false;
		}
		TArray<FString> Lines;
		Log.ParseIntoArrayLines(Lines, false);
		const int32 First = FMath::Max(0, Lines.Num() - GReportLogLines);
		return FFileHelper::SaveStringToFile(FString::Join(TArrayView<const FString>(Lines).RightChop(First), TEXT("\n")), *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

namespace ErewhonReport
{
	FText CategoryLabel(ECategory Category)
	{
		switch (Category)
		{
		case ECategory::Observation: return LOCTEXT("Observation", "Constat");
		case ECategory::Idea: return LOCTEXT("Idea", "Idée");
		default: return LOCTEXT("Bug", "Bug");
		}
	}

	FString FCapture::Find(const FString& Section, const FString& Key) const
	{
		for (const FSection& Each : Context)
		{
			if (Each.Name == Section)
			{
				for (const TPair<FString, FString>& Value : Each.Values)
				{
					if (Value.Key == Key)
					{
						return Value.Value;
					}
				}
			}
		}
		return FString();
	}

	FCapture Capture(AErewhonPlayerController* Controller)
	{
		FCapture Result;
		Result.Time = FDateTime::Now();
		if (Controller && Controller->GetWorld())
		{
			CaptureScreen(Controller, Result);
			Result.Context = CaptureContext(Controller, Result.Time);
		}
		return Result;
	}

	FString Save(const FCapture& Capture, ECategory Category, const FString& Title, const FString& Description, bool bWithScreenshot)
	{
		const FString Name = Title.IsEmpty() ? ReportCategoryId(Category) : Title;
		const FString Base = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Reports"))
			/ Capture.Time.ToString(TEXT("%Y-%m-%d_%H-%M-%S_")) + ReportSlug(Name);
		FString Folder = Base;
		for (int32 Suffix = 2; IFileManager::Get().DirectoryExists(*Folder); ++Suffix)
		{
			Folder = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
		}
		if (!IFileManager::Get().MakeDirectory(*Folder, true))
		{
			UE_LOG(LogErewhonReport, Error, TEXT("Impossible de créer %s"), *Folder);
			return FString();
		}

		const bool bScreenshot = bWithScreenshot && !Capture.Pixels.IsEmpty()
			&& FImageUtils::SaveImageByExtension(*(Folder / TEXT("screenshot.png")), FImageView(Capture.Pixels.GetData(), Capture.Size.X, Capture.Size.Y));
		const FString Shown = Title.IsEmpty() ? CategoryLabel(Category).ToString() : Title;
		bool bOk = FFileHelper::SaveStringToFile(ReportMarkdown(Capture, Category, Shown, Description, bScreenshot), *(Folder / TEXT("report.md")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		bOk &= FFileHelper::SaveStringToFile(ReportJson(Capture, Category, Shown, Description, bScreenshot), *(Folder / TEXT("context.json")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!SaveLogTail(Folder / TEXT("log.txt")))
		{
			UE_LOG(LogErewhonReport, Warning, TEXT("Log du jeu illisible : log.txt absent du signalement"));
		}
		UE_LOG(LogErewhonReport, Log, TEXT("Signalement enregistré : %s"), *Folder);
		return bOk ? Folder : FString();
	}
}

#undef LOCTEXT_NAMESPACE
