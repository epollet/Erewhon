#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"

struct FItemDef;
struct FSlateBrush;

/** Style commun des panneaux : bois sombre et parchemin, accents par type de slot. */
namespace ErewhonUI
{
	inline const FLinearColor PanelBackground(0.075f, 0.06f, 0.045f, 0.94f);
	inline const FLinearColor PanelBorder(0.55f, 0.42f, 0.26f, 1.f);
	inline const FLinearColor TextColor(0.98f, 0.95f, 0.88f, 1.f);
	inline const FLinearColor MutedText(0.70f, 0.66f, 0.58f, 1.f);
	inline const FLinearColor CoinColor(1.f, 0.78f, 0.30f, 1.f);
	inline const FLinearColor InventoryAccent(0.26f, 0.47f, 0.80f, 1.f);
	inline const FLinearColor ClothingAccent(0.33f, 0.62f, 0.35f, 1.f);
	inline const FLinearColor EquipmentAccent(0.88f, 0.56f, 0.20f, 1.f);

	const FSlateBrush* WhiteBrush();
	FSlateFontInfo Font(int32 Size, bool bBold = false);

	/** Cadre de fenêtre : titre, bouton de fermeture et contenu. */
	TSharedRef<SWidget> Window(const FText& Title, TSharedRef<SWidget> Content, FSimpleDelegate OnClose, float Width);

	/** Vignette d'objet : icône générée si disponible, sinon pastille de couleur. */
	TSharedRef<SWidget> ItemIcon(const FItemDef* Def, float Size);

	/** Bulle d'aide : nom en gras, description dessous. */
	TSharedRef<class SToolTip> ItemTooltip(const FItemDef& Def);

	TSharedRef<SWidget> Separator();
}
