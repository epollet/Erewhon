#include "World/Signpost.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "World/ErewhonVisuals.h"
#include "World/SpawnLayout.h"

using namespace ErewhonVisuals;

namespace ErewhonSigns
{
	namespace
	{
		constexpr float GPostHeight = 250.f;
		constexpr float GArmHeight = 30.f;
		constexpr float GArmThickness = 4.f;
		constexpr float GArmSpacing = 38.f;

		/** Résolution de l'inscription : pixels pour la hauteur d'une planche. */
		constexpr int32 GPlankPixels = 128;
		/** Hauteur des majuscules dans la planche (px). */
		constexpr float GCapPixels = 54.f;
		const FLinearColor GInk = FLinearColor(0.025f, 0.014f, 0.007f);
		const FLinearColor GPaint = FLinearColor::FromSRGBColor(FColor(236, 222, 190));

		/**
		 * Police manuscrite (Kalam Bold, licence OFL), précalculée par Tools/assetgen/font_atlas.py :
		 * l'atlas T_font_hand et les mesures de chaque caractère (Content/Data/FontHand.json).
		 */
		struct FHandFont
		{
			struct FGlyph { FVector2D Pos, Size; float XOffset = 0.f, Advance = 0.f; };
			TMap<TCHAR, FGlyph> Glyphs;
			FVector2D AtlasSize = FVector2D(1.f);
			float FontSize = 96.f;
			float Ascent = 0.f;
			float Pad = 6.f;
			TWeakObjectPtr<UTexture> Atlas;

			float Scale() const { return GCapPixels / (FontSize * 0.7f); }

			static const FHandFont& Get()
			{
				static FHandFont Font = Load();
				if (!Font.Atlas.IsValid())
				{
					Font.Atlas = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_font_hand"));
				}
				return Font;
			}

			static FHandFont Load()
			{
				FHandFont Font;
				FString Json;
				TSharedPtr<FJsonObject> Root;
				if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/FontHand.json")))
					|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
				{
					return Font;
				}
				Font.FontSize = Root->GetNumberField(TEXT("size"));
				Font.Ascent = Root->GetNumberField(TEXT("ascent"));
				Font.AtlasSize = FVector2D(Root->GetNumberField(TEXT("width")), Root->GetNumberField(TEXT("height")));
				for (const auto& Pair : Root->GetObjectField(TEXT("glyphs"))->Values)
				{
					const TSharedPtr<FJsonObject> G = Pair.Value->AsObject();
					const int32 Code = FCString::Atoi(*FString(Pair.Key));   // clé = code Unicode du caractère
					if (!G.IsValid() || Code <= 0)
					{
						continue;
					}
					FGlyph& Glyph = Font.Glyphs.Add(static_cast<TCHAR>(Code));
					Glyph.Pos = FVector2D(G->GetNumberField(TEXT("x")), G->GetNumberField(TEXT("y")));
					Glyph.Size = FVector2D(G->GetNumberField(TEXT("w")), G->GetNumberField(TEXT("h")));
					Glyph.XOffset = G->GetNumberField(TEXT("xoff"));
					Glyph.Advance = G->GetNumberField(TEXT("advance"));
				}
				return Font;
			}

			/** Largeur du texte dans la planche (px). */
			float Measure(const FString& Text) const
			{
				float Width = 0.f;
				for (const TCHAR Ch : Text)
				{
					const FGlyph* Glyph = Glyphs.Find(Ch);
					Width += Glyph ? Glyph->Advance : FontSize * 0.4f;
				}
				return Width * Scale();
			}

			/** Dessine le texte centré sur Center (px), lettre par lettre. */
			void Draw(UCanvas* Canvas, const FString& Text, const FVector2D& Center, const FLinearColor& Color) const
			{
				UTexture* Texture = Atlas.Get();
				if (!Texture)
				{
					return;
				}
				const float S = Scale();
				float PenX = Center.X - Measure(Text) * 0.5f;
				const float Baseline = Center.Y + GCapPixels * 0.5f;
				for (const TCHAR Ch : Text)
				{
					const FGlyph* Glyph = Glyphs.Find(Ch);
					if (!Glyph)
					{
						PenX += FontSize * 0.4f * S;
						continue;
					}
					// Dans l'atlas, la ligne de base est à Pad + Ascent sous le haut de la case.
					const FVector2D Position(PenX + Glyph->XOffset * S, Baseline - (Pad + Ascent) * S);
					Canvas->K2_DrawTexture(Texture, Position, Glyph->Size * S, Glyph->Pos / AtlasSize, Glyph->Size / AtlasSize,
						Color, BLEND_Translucent);
					PenX += Glyph->Advance * S;
				}
			}
		};

		float MeasureText(const FString& Text)
		{
			return FHandFont::Get().Measure(Text);
		}

		/** Peint la planche claire et son inscription à l'encre sombre dans la texture. */
		void DrawPlank(UObject* WorldContext, UTextureRenderTarget2D* Target, const FString& Text)
		{
			UCanvas* Canvas = nullptr;
			FVector2D Size;
			FDrawToRenderTargetContext Context;
			UKismetRenderingLibrary::ClearRenderTarget2D(WorldContext, Target, GPaint);
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(WorldContext, Target, Canvas, Size, Context);
			if (Canvas)
			{
				// Bois blanchi : le veinage des planches, à peine visible sous la peinture claire.
				if (UTexture* Wood = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_wood_planks")))
				{
					const float Repeat = 0.35f;
					Canvas->K2_DrawTexture(Wood, FVector2D::ZeroVector, Size, FVector2D::ZeroVector,
						FVector2D(Repeat * Size.X / Size.Y, Repeat), FLinearColor(1.f, 1.f, 1.f, 0.22f), BLEND_Translucent);
				}
				FHandFont::Get().Draw(Canvas, Text, Size * 0.5f, GInk);
			}
			UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(WorldContext, Context);
		}

		/**
		 * Texture de l'inscription. Le dessin est différé : peint pendant le chargement du monde,
		 * il serait perdu. On repeint une seconde fois un peu plus tard, quand l'atlas de la
		 * police est sûrement chargé en pleine résolution.
		 */
		UTexture* PaintPlank(AActor* Owner, const FString& Text, int32 Width)
		{
			UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(Owner, Width, GPlankPixels, RTF_RGBA8_SRGB, GPaint);
			UWorld* World = Owner->GetWorld();
			if (!Target || !World)
			{
				return Target;
			}
			const TWeakObjectPtr<AActor> WeakOwner(Owner);
			const TWeakObjectPtr<UTextureRenderTarget2D> WeakTarget(Target);
			for (const float Delay : { 0.3f, 2.5f })
			{
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakOwner, WeakTarget, Text]()
				{
					if (WeakOwner.IsValid() && WeakTarget.IsValid())
					{
						DrawPlank(WeakOwner.Get(), WeakTarget.Get(), Text);
					}
				}), Delay, false);
			}
			return Target;
		}
	}

	void AddSignpost(AActor* Owner, USceneComponent* Parent, const FVector& Location, const TArray<FArm>& Arms)
	{
		if (!Owner || !Parent)
		{
			return;
		}
		// Poteau de bois sombre, coiffé d'un petit chapeau.
		UStaticMeshComponent* Post = AddShape(Owner, Parent, EShape::Cube, Location + FVector(0, 0, GPostHeight * 0.5f),
			FVector(11, 11, GPostHeight), Palette::WoodDark);
		Post->SetMaterial(0, MakePaintedMaterial(Post, TEXT("wood_trim"), 120.f, Palette::WoodDark));
		AddShape(Owner, Parent, EShape::Cone, Location + FVector(0, 0, GPostHeight), FVector(18, 18, 12), Palette::WoodDark,
			FRotator::ZeroRotator, false);

		for (int32 Index = 0; Index < Arms.Num(); ++Index)
		{
			const FArm& Arm = Arms[Index];
			USceneComponent* Frame = NewObject<USceneComponent>(Owner);
			Frame->SetupAttachment(Parent);
			Frame->SetRelativeLocationAndRotation(Location + FVector(0, 0, GPostHeight - 28.f - Index * GArmSpacing),
				FRotator(0.f, Arm.Yaw, 0.f));
			Frame->RegisterComponent();
			Owner->AddInstanceComponent(Frame);

			// Planche le long de +X, terminée en pointe (un carré posé sur l'angle), longue comme son texte.
			const float CmPerPixel = GArmHeight / GPlankPixels;
			const float Length = FMath::Max(60.f, (MeasureText(Arm.Text) + 56.f) * CmPerPixel);
			const float Start = 7.f;
			UMaterialInterface* Wood = MakePaintedMaterial(Frame, TEXT("wood_trim"), 90.f, Palette::WoodDark);
			UStaticMeshComponent* Plank = AddShape(Owner, Frame, EShape::Cube, FVector(Start + Length * 0.5f, 0, 0),
				FVector(Length, GArmThickness, GArmHeight), Palette::WoodDark, FRotator::ZeroRotator, false);
			Plank->SetMaterial(0, Wood);
			const float Tip = GArmHeight / UE_SQRT_2;
			UStaticMeshComponent* Point = AddShape(Owner, Frame, EShape::Cube, FVector(Start + Length, 0, 0),
				FVector(Tip, GArmThickness, Tip), Palette::WoodDark, FRotator(45.f, 0.f, 0.f), false);
			Point->SetMaterial(0, Wood);

			// L'inscription peinte, sur les deux faces (la planche sombre lui fait un liseré).
			UTexture* Painted = PaintPlank(Owner, Arm.Text, FMath::RoundToInt32(Length / CmPerPixel / 4.f) * 4);
			for (const float Side : { -1.f, 1.f })
			{
				AddPicture(Owner, Frame, FVector(Start + Length * 0.5f, Side * (GArmThickness * 0.5f + 0.3f), 0.f),
					Length - 4.f, GArmHeight - 4.f, Side > 0.f ? -90.f : 90.f, Painted);
			}
		}
	}

	FArm ShopArm(EShopKind Kind, const FVector& From, float Yaw)
	{
		// Les chemins vont à angle droit : la distance à pied est celle « de Manhattan ».
		const FVector To = ErewhonSpawn::DoorstepLocation(Kind);
		const float Meters = (FMath::Abs(To.X - From.X) + FMath::Abs(To.Y - From.Y)) / 100.f;
		const int32 Rounded = FMath::Max(10, FMath::RoundToInt32(Meters / 10.f) * 10);
		return { FString::Printf(TEXT("%s  %d m"), *ErewhonSpawn::ShopName(Kind), Rounded), Yaw };
	}
}
