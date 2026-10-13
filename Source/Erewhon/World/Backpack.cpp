#include "World/Backpack.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Items/ItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "World/ErewhonVisuals.h"

#define LOCTEXT_NAMESPACE "ErewhonBackpack"

using namespace ErewhonVisuals;

namespace ErewhonBackpack
{
	namespace
	{
		const FLinearColor GCanvas = FLinearColor::FromSRGBColor(FColor(112, 128, 72));
		const FLinearColor GCanvasDark = FLinearColor::FromSRGBColor(FColor(90, 106, 58));
		const FLinearColor GLeather = FLinearColor::FromSRGBColor(FColor(124, 72, 40));
		const FLinearColor GBrass = FLinearColor::FromSRGBColor(FColor(200, 160, 76));
	}

	TArray<UStaticMeshComponent*> Build(AActor* Owner, USceneComponent* Parent, bool bCollision)
	{
		const auto Part = [Owner, Parent](EShape Shape, const FVector& Center, const FVector& Size, const FLinearColor& Color,
			const FRotator& Rotation = FRotator::ZeroRotator, bool bSolid = false)
		{
			UStaticMeshComponent* Piece = AddShape(Owner, Parent, Shape, Center, Size, Color, Rotation, bSolid);
			Piece->SetCastShadow(true);
			return Piece;
		};
		// Corps de toile, dessus arrondi.
		Part(EShape::Cube, FVector(0, 0, 21), FVector(24, 36, 42), GCanvas, FRotator::ZeroRotator, bCollision);
		Part(EShape::Cylinder, FVector(0, 0, 42), FVector(24, 24, 36), GCanvas, FRotator(0.f, 0.f, 90.f));
		// Rabat qui retombe sur le devant, et ses deux sangles bouclées.
		Part(EShape::Cube, FVector(11.5f, 0, 39), FVector(4, 35, 18), GCanvasDark);
		for (const float Y : { -9.f, 9.f })
		{
			Part(EShape::Cube, FVector(13.8f, Y, 37), FVector(1.5f, 4, 30), GLeather);
			Part(EShape::Cube, FVector(14.8f, Y, 28), FVector(1.2f, 5, 4), GBrass);
			// Poche avant et sa patte.
			Part(EShape::Cube, FVector(15, Y, 12), FVector(7, 14, 15), GCanvasDark);
			Part(EShape::Cube, FVector(18.8f, Y, 14), FVector(1.2f, 3, 12), GLeather);
			// Bretelles, côté dos.
			Part(EShape::Cube, FVector(-13, Y * 1.1f, 24), FVector(2, 5, 40), GLeather);
		}
		// Passants de cuir de chaque côté, où pendent les outils.
		TArray<UStaticMeshComponent*> Holders;
		for (const float Side : { -1.f, 1.f })
		{
			Part(EShape::Cube, FVector(0, Side * 18.6f, 30), FVector(6, 1.5f, 4), GLeather);
			Part(EShape::Cube, FVector(0, Side * 18.6f, 12), FVector(6, 1.5f, 4), GLeather);
			UStaticMeshComponent* Holder = NewObject<UStaticMeshComponent>(Owner);
			Holder->SetupAttachment(Parent);
			Holder->SetRelativeLocationAndRotation(FVector(0, Side * 21.f, 4.f), FRotator(0.f, 0.f, Side * 6.f));
			Holder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Holder->SetVisibility(false);
			Holder->RegisterComponent();
			Owner->AddInstanceComponent(Holder);
			Holders.Add(Holder);
		}
		return Holders;
	}

	void SetTools(const TArray<UStaticMeshComponent*>& Holders, const TArray<FName>& Tools)
	{
		const UItemDatabase* Items = Holders.Num() > 0 && Holders[0] ? UItemDatabase::Get(Holders[0]) : nullptr;
		for (int32 i = 0; i < Holders.Num(); ++i)
		{
			UStaticMeshComponent* Holder = Holders[i];
			if (!Holder)
			{
				continue;
			}
			const FItemDef* Def = Items && Tools.IsValidIndex(i) ? Items->Find(Tools[i]) : nullptr;
			UStaticMesh* Mesh = Def ? Cast<UStaticMesh>(Def->Mesh.TryLoad()) : nullptr;
			Holder->SetVisibility(Def != nullptr);
			if (!Def)
			{
				continue;
			}
			// Point d'accroche fixe, de ce côté du sac (le recalcul ne doit pas dériver).
			const FVector Base(0.f, FMath::Sign(Holder->GetRelativeLocation().Y) * 21.f, 4.f);
			if (Mesh)
			{
				// Le manche glissé dans les passants, la lame vers le haut.
				const FTransform Fit = FitToHeight(Mesh, 46.f);
				Holder->SetStaticMesh(Mesh);
				Holder->SetRelativeScale3D(Fit.GetScale3D());
				Holder->SetRelativeLocation(Base + Holder->GetRelativeRotation().RotateVector(Fit.GetLocation()));
				Holder->EmptyOverrideMaterials();
			}
			else
			{
				Holder->SetStaticMesh(GetShape(EShape::Cube));
				Holder->SetRelativeScale3D(FVector(0.04f, 0.04f, 0.46f));
				Holder->SetRelativeLocation(Base + FVector(0, 0, 23.f));
				Holder->SetMaterial(0, MakeColorMaterial(Holder, Def->Color));
			}
		}
	}
}

// ── Sac posé ─────────────────────────────────────────────────────────────────

ABackpack::ABackpack()
{
	bReplicates = true;
	SetReplicatingMovement(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void ABackpack::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABackpack, OwnerKey);
	DOREPLIFETIME(ABackpack, OwnerName);
	DOREPLIFETIME(ABackpack, SideTools);
}

void ABackpack::BeginPlay()
{
	Super::BeginPlay();
	// La collision du corps sert aussi au ciblage (canal Visibility).
	for (UStaticMeshComponent* Holder : ErewhonBackpack::Build(this, Root, true))
	{
		ToolHolders.Add(Holder);
	}
	OnRep_SideTools();
}

void ABackpack::SetSideTools(const TArray<FName>& Tools)
{
	SideTools = Tools;
	OnRep_SideTools();
}

void ABackpack::OnRep_SideTools()
{
	TArray<UStaticMeshComponent*> Holders;
	for (const TWeakObjectPtr<UStaticMeshComponent>& Holder : ToolHolders)
	{
		Holders.Add(Holder.Get());
	}
	ErewhonBackpack::SetTools(Holders, SideTools);
}

FText ABackpack::GetInteractionLabel() const
{
	const UWorld* World = GetWorld();
	const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
	const AErewhonPlayerState* State = Local ? Local->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (State && IsOwnedBy(State->GetPlayerKey()))
	{
		return LOCTEXT("Mine", "Ouvrir le sac    [F] Le remettre sur le dos    [G] Tout y ranger");
	}
	return FText::Format(LOCTEXT("Theirs", "Sac à dos de {0}"), FText::FromString(OwnerName));
}

void ABackpack::Interact(AErewhonPlayerController* Controller)
{
	const AErewhonPlayerState* State = Controller ? Controller->GetPlayerState<AErewhonPlayerState>() : nullptr;
	if (!State)
	{
		return;
	}
	if (IsOwnedBy(State->GetPlayerKey()))
	{
		Controller->OpenInventory(this);
	}
	else
	{
		Controller->ShowToast(LOCTEXT("NotYourBag", "Ce sac n'est pas à vous."));
	}
}

#undef LOCTEXT_NAMESPACE
