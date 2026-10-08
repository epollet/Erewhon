#include "World/Sector.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Core/ErewhonSettings.h"
#include "Engine/StaticMesh.h"
#include "Items/ItemDatabase.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorLayout.h"

using namespace ErewhonVisuals;

namespace
{
	/** Une partie de placeholder, dimensionnée pour une plante de 1 m de haut (cm). */
	struct FPartSpec
	{
		EShape Shape;
		FVector Center;
		FVector Size;
		FRotator Rotation;
		FLinearColor Color;
	};

	TArray<FPartSpec> PlaceholderParts(const FItemDef& Def)
	{
		const FLinearColor Accent = Def.Plant.AccentColor;
		switch (Def.Plant.Shape)
		{
		case EPlantShape::Flower:
			return {
				{ EShape::Cylinder, FVector(0, 0, 50), FVector(4, 4, 100), FRotator::ZeroRotator, Palette::Stem },
				{ EShape::Sphere, FVector(0, 0, 100), FVector(42, 42, 12), FRotator::ZeroRotator, Def.Color },
				{ EShape::Sphere, FVector(0, 0, 105), FVector(16, 16, 10), FRotator::ZeroRotator, Accent },
			};
		case EPlantShape::Herb:
			return {
				{ EShape::Sphere, FVector(0, 0, 22), FVector(64, 64, 44), FRotator::ZeroRotator, Accent },
				{ EShape::Sphere, FVector(0, 0, 58), FVector(34, 34, 34), FRotator::ZeroRotator, Def.Color },
			};
		case EPlantShape::Grass:
		default:
			// Touffe de deux brins, le second couché par le vent.
			return {
				{ EShape::Cone, FVector(0, 0, 50), FVector(12, 12, 100), FRotator::ZeroRotator, Def.Color },
				{ EShape::Cone, FVector(8, 0, 36), FVector(10, 10, 75), FRotator(-18, 0, 0), Def.Color * 0.85f },
			};
		}
	}
}

ASector::ASector()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

FIntPoint ASector::GetCoord() const
{
	return Layout.IsValid() ? Layout->Coord : FIntPoint::ZeroValue;
}

void ASector::Initialize(const TSharedRef<const FSectorLayout>& InLayout, bool bWithPlants)
{
	Layout = InLayout;
	SetActorLocation(ErewhonSector::Origin(InLayout->Coord));
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Sector_%s"), *ErewhonSector::ToString(InLayout->Coord)));
#endif
	BuildGround();
	if (bWithPlants)
	{
		BuildPlants();
	}
}

void ASector::BuildGround()
{
	const float Size = UErewhonSettings::Get().SectorSize();
	const FLinearColor Grass = Layout->Type == ESectorType::Spawn ? Palette::MeadowSpawn : Palette::Meadow;

	// Dalle d'un mètre d'épaisseur dont le dessus affleure Z = 0.
	UStaticMeshComponent* Ground = AddShape(this, Root, EShape::Cube, FVector(Size * 0.5f, Size * 0.5f, -50.f), FVector(Size, Size, 100.f), Grass);
	Ground->SetCastShadow(false);

	const FVector Origin = GetActorLocation();
	for (const FBox2D& Path : Layout->Paths)
	{
		const FVector2D Center = Path.GetCenter() - FVector2D(Origin);
		const FVector2D Extent = Path.GetSize();
		UStaticMeshComponent* Dirt = AddShape(this, Root, EShape::Cube, FVector(Center.X, Center.Y, 0.5f), FVector(Extent.X, Extent.Y, 2.f), Palette::Dirt);
		Dirt->SetCastShadow(false);
	}
}

ASector::FPlantKind& ASector::FindOrCreateKind(FName ItemId)
{
	for (FPlantKind& Kind : Kinds)
	{
		if (Kind.ItemId == ItemId)
		{
			return Kind;
		}
	}

	FPlantKind& Kind = Kinds.AddDefaulted_GetRef();
	Kind.ItemId = ItemId;

	const FItemDef* Def = UItemDatabase::Get(this)->Find(ItemId);
	const auto MakePart = [this, Def](UStaticMesh* Mesh, UMaterialInterface* Material) -> UHierarchicalInstancedStaticMeshComponent*
	{
		UHierarchicalInstancedStaticMeshComponent* Part = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		Part->SetStaticMesh(Mesh);
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
		Part->SetupAttachment(Root);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->bAffectDistanceFieldLighting = false;
		const int32 CullEnd = FMath::RoundToInt32(Def->Plant.CullDistanceMeters * 100.f);
		Part->SetCullDistances(CullEnd * 3 / 4, CullEnd);
		Part->RegisterComponent();
		AddInstanceComponent(Part);
		return Part;
	};

	if (UStaticMesh* Generated = Cast<UStaticMesh>(Def->Mesh.TryLoad()))
	{
		// Mesh généré : supposé normalisé à 1 m de haut, pivot à la base.
		Kind.Parts.Add(MakePart(Generated, nullptr));
		Kind.PartLocal.Add(FTransform::Identity);
		return Kind;
	}

	for (const FPartSpec& Spec : PlaceholderParts(*Def))
	{
		UHierarchicalInstancedStaticMeshComponent* Part = MakePart(GetShape(Spec.Shape), nullptr);
		Part->SetMaterial(0, MakeColorMaterial(Part, Spec.Color));
		Kind.Parts.Add(Part);
		Kind.PartLocal.Add(FTransform(Spec.Rotation, Spec.Center, Spec.Size / 100.f));
	}
	return Kind;
}

FTransform ASector::PlantTransform(int32 PlantIndex) const
{
	const FPlantInstance& Plant = Layout->Plants[PlantIndex];
	// Relatif à l'acteur ; le gabarit fait 1 m, la hauteur en mètres sert donc d'échelle.
	return FTransform(FRotator(0, Plant.Yaw, 0), Plant.Location - GetActorLocation(), FVector(Plant.Height));
}

void ASector::BuildPlants()
{
	const int32 Count = Layout->Plants.Num();
	KindOfPlant.SetNumUninitialized(Count);
	InstanceOfPlant.SetNumUninitialized(Count);

	// Regroupe les transformations par espèce pour un ajout en lot.
	TArray<TArray<TArray<FTransform>>> Batches;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FName ItemId = Layout->Plants[Index].ItemId;
		FindOrCreateKind(ItemId);
		const int32 KindIndex = Kinds.IndexOfByPredicate([ItemId](const FPlantKind& K) { return K.ItemId == ItemId; });
		if (Batches.Num() <= KindIndex)
		{
			Batches.SetNum(KindIndex + 1);
		}
		FPlantKind& Kind = Kinds[KindIndex];
		Batches[KindIndex].SetNum(Kind.Parts.Num());

		const FTransform Plant = PlantTransform(Index);
		KindOfPlant[Index] = KindIndex;
		InstanceOfPlant[Index] = Batches[KindIndex][0].Num();
		for (int32 PartIndex = 0; PartIndex < Kind.Parts.Num(); ++PartIndex)
		{
			Batches[KindIndex][PartIndex].Add(Kind.PartLocal[PartIndex] * Plant);
		}
	}

	for (int32 KindIndex = 0; KindIndex < Kinds.Num(); ++KindIndex)
	{
		for (int32 PartIndex = 0; PartIndex < Kinds[KindIndex].Parts.Num(); ++PartIndex)
		{
			Kinds[KindIndex].Parts[PartIndex]->AddInstances(Batches[KindIndex][PartIndex], false, false);
		}
	}
	bPlantsBuilt = true;
}

void ASector::SetPlantHarvested(int32 PlantIndex, bool bHarvested)
{
	if (!bPlantsBuilt || !KindOfPlant.IsValidIndex(PlantIndex))
	{
		return;
	}
	FPlantKind& Kind = Kinds[KindOfPlant[PlantIndex]];
	const int32 Instance = InstanceOfPlant[PlantIndex];

	// Une plante récoltée est réduite à rien plutôt que retirée : les index restent stables.
	FTransform Plant = PlantTransform(PlantIndex);
	if (bHarvested)
	{
		Plant.SetScale3D(FVector(KINDA_SMALL_NUMBER));
	}
	for (int32 PartIndex = 0; PartIndex < Kind.Parts.Num(); ++PartIndex)
	{
		Kind.Parts[PartIndex]->UpdateInstanceTransform(Instance, Kind.PartLocal[PartIndex] * Plant, false, true, false);
	}
}
