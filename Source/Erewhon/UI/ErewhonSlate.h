#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"

struct FButtonStyle;
struct FItemDef;
struct FSlateBrush;
class SToolTip;

/**
 * Style commun des panneaux, façon carnet pastoral : parchemin clair aux coins arrondis,
 * encre brune, accents feuille, argile et ciel. Fond du jeu flouté derrière les menus.
 */
namespace ErewhonUI
{
	// ── Palette ─────────────────────────────────────────────────────────────

	inline const FLinearColor Parchment = FLinearColor::FromSRGBColor(FColor(0xF3, 0xE7, 0xCC));
	inline const FLinearColor Card = FLinearColor::FromSRGBColor(FColor(0xFB, 0xF4, 0xE4));
	inline const FLinearColor Edge = FLinearColor::FromSRGBColor(FColor(0x9E, 0x7A, 0x4E));
	inline const FLinearColor Ink = FLinearColor::FromSRGBColor(FColor(0x3D, 0x2C, 0x1E));
	inline const FLinearColor MutedInk = FLinearColor::FromSRGBColor(FColor(0x7F, 0x6A, 0x52));
	inline const FLinearColor Leaf = FLinearColor::FromSRGBColor(FColor(0x6C, 0x9A, 0x4D));
	inline const FLinearColor Clay = FLinearColor::FromSRGBColor(FColor(0xC7, 0x73, 0x4D));
	inline const FLinearColor Sky = FLinearColor::FromSRGBColor(FColor(0x5C, 0x8D, 0xB8));
	inline const FLinearColor Gold = FLinearColor::FromSRGBColor(FColor(0xD9, 0xA0, 0x30));
	inline const FLinearColor Danger = FLinearColor::FromSRGBColor(FColor(0xB5, 0x53, 0x3C));
	inline const FLinearColor RoadColor = FLinearColor::FromSRGBColor(FColor(0xC9, 0xA2, 0x6B));

	/** Accents des types de slots d'inventaire. */
	inline const FLinearColor InventoryAccent = Sky;
	inline const FLinearColor ClothingAccent = Leaf;
	inline const FLinearColor EquipmentAccent = Clay;

	/** Mélange de deux couleurs (t = 0 → A). */
	inline FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T) { return FLinearColor::LerpUsingHSV(A, B, T); }

	// ── Briques ─────────────────────────────────────────────────────────────

	enum class EButton : uint8 { Primary, Secondary, Danger, Tab, TabActive };

	FSlateFontInfo Font(int32 Size, bool bBold = false);
	const FSlateBrush* WhiteBrush();

	/** Rectangle arrondi, rempli et éventuellement cerné (mis en cache par paramètres). */
	const FSlateBrush* Rounded(const FLinearColor& Fill, float Radius = 8.f, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);

	const FButtonStyle& ButtonStyle(EButton Kind);
	FLinearColor ButtonTextColor(EButton Kind);

	/** Bouton arrondi avec libellé. */
	TSharedRef<SWidget> Button(const FText& Label, TFunction<void()> OnClick, EButton Kind = EButton::Secondary,
		bool bEnabled = true, float MinWidth = 0.f, int32 FontSize = 12);

	/** Pastille de pièces (« 120 C »). */
	TSharedRef<SWidget> CoinBadge(int32 Amount, int32 FontSize = 13);

	/** Fenêtre centrée sur fond flouté : titre, sous-titre, bouton de fermeture, contenu. */
	TSharedRef<SWidget> Window(const FText& Title, TSharedRef<SWidget> Content, FSimpleDelegate OnClose, float Width,
		const FText& Subtitle = FText::GetEmpty());

	/** Carte : bloc clair arrondi, pour regrouper un contenu dans une fenêtre. */
	TSharedRef<SWidget> CardBox(TSharedRef<SWidget> Content, float Padding = 12.f);

	/** Vignette d'objet : icône générée si disponible, sinon pastille de couleur. */
	TSharedRef<SWidget> ItemIcon(const FItemDef* Def, float Size);

	/** Bulle d'aide : nom en gras, description dessous. */
	TSharedRef<SToolTip> ItemTooltip(const FItemDef& Def);

	/** Filet de séparation discret. */
	TSharedRef<SWidget> Separator();
}
