#include "World/Storage.h"
#include "World/Forestry.h"
#include "GameFramework/Character.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Items/ItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "World/ErewhonVisuals.h"
#include "World/Apothecary.h"
#include "World/Terrain.h"
#include "World/Village.h"
#include "Sound/SoundWave.h"
#include "World/ErewhonSound.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Player/ErewhonCharacter.h"
#include "Core/ErewhonGameState.h"
#include "World/SectorTypes.h"
#include "World/Bhurak.h"
#include "World/Farm.h"

#define LOCTEXT_NAMESPACE "ErewhonStorage"

using namespace ErewhonVisuals;

namespace
{
	/** Le modèle généré de la charrette a son grand côté sur Y : un quart de tour l'aligne sur X, brancards vers −X. */
	constexpr float GCartMeshYaw = -90.f;
	constexpr float GCartLength = 260.f;

	/** Caisse sans roues (les roues sont des pièces à part, qui tournent) : même règle d'orientation. */
	constexpr float GCartBodyYaw = 90.f;
	constexpr float GBodyLength = 230.f;
	/** Décalage de la caisse pour que son moyeu tombe sur l'essieu des roues. */
	const FVector GBodyOffset(-14.f, 0.f, 14.f);

	const FString& LocalPlayerKey(const UObject* Context)
	{
		static const FString None;
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		const APlayerController* Local = World ? World->GetFirstPlayerController() : nullptr;
		const AErewhonPlayerState* State = Local ? Local->GetPlayerState<AErewhonPlayerState>() : nullptr;
		return State ? State->GetPlayerKey() : None;
	}
}

namespace
{
	/** Rotation des modèles d'outils tractés pour que leurs brancards pointent vers −X. */
	constexpr float GCultivatorYaw = 270.f;
	constexpr float GSeederYaw = 270.f;
	constexpr float GHarvesterYaw = 270.f;
}

namespace ErewhonPlaceables
{
	TOptional<EStorageKind> StorageForItem(FName ItemId)
	{
		if (ItemId == TEXT("crate"))
		{
			return EStorageKind::Crate;
		}
		if (ItemId == TEXT("barrel"))
		{
			return EStorageKind::Barrel;
		}
		if (ItemId == TEXT("shelf"))
		{
			return EStorageKind::Shelf;
		}
		if (ItemId == TEXT("table"))
		{
			return EStorageKind::Table;
		}
		if (ItemId == TEXT("mortar"))
		{
			return EStorageKind::Mortar;
		}
		if (ItemId == TEXT("cauldron"))
		{
			return EStorageKind::Cauldron;
		}
		if (ItemId == TEXT("drying_rack"))
		{
			return EStorageKind::DryingRack;
		}
		if (ItemId == TEXT("workbench"))
		{
			return EStorageKind::Workbench;
		}
		if (ItemId == TEXT("potpourri"))
		{
			return EStorageKind::Potpourri;
		}
		if (ItemId == TEXT("cauldron_stand"))
		{
			return EStorageKind::CauldronStand;
		}
		if (ItemId == TEXT("firewood_rack"))
		{
			return EStorageKind::FirewoodRack;
		}
		if (ItemId == ErewhonForestry::Log)
		{
			return EStorageKind::LogPile;   // des bûches en main : on en fait un tas
		}
		return {};
	}

	FName ItemForStorage(EStorageKind Kind)
	{
		switch (Kind)
		{
		case EStorageKind::Barrel:   return TEXT("barrel");
		case EStorageKind::Shelf:    return TEXT("shelf");
		case EStorageKind::Table:    return TEXT("table");
		case EStorageKind::Mortar:   return TEXT("mortar");
		case EStorageKind::Cauldron: return TEXT("cauldron");
		case EStorageKind::DryingRack: return TEXT("drying_rack");
		case EStorageKind::Workbench:  return TEXT("workbench");
		case EStorageKind::Potpourri:  return TEXT("potpourri");
		case EStorageKind::CauldronStand: return TEXT("cauldron_stand");
		case EStorageKind::FirewoodRack:  return TEXT("firewood_rack");
		case EStorageKind::LogPile:       return ErewhonForestry::Log;
		default:                     return TEXT("crate");
		}
	}

	bool IsCart(FName ItemId)
	{
		return CartKindForItem(ItemId).IsSet();
	}

	bool IsBhurak(FName ItemId)
	{
		return ABhurak::CoatOfItem(ItemId) != INDEX_NONE;
	}

	int32 Capacity(EStorageKind Kind)
	{
		// Le tonneau contient deux cagettes et demie ; l'étagère, une pile par étage.
		switch (Kind)
		{
		case EStorageKind::Crate:  return 12;
		case EStorageKind::Barrel: return 30;
		case EStorageKind::Shelf:  return ErewhonApothecary::ShelfSlots;
		case EStorageKind::DryingRack: return 6;
		case EStorageKind::Cauldron: return 6;   // ses ingrédients
		case EStorageKind::FirewoodRack: return 4;
		case EStorageKind::Saddlebag: return 12;   // un sac à dos
		case EStorageKind::LogPile: return ErewhonForestry::PileCapacity / ErewhonForestry::LogCarry;   // des places de deux
		default:                   return 0;
		}
	}

	bool IsFurniture(EStorageKind Kind)
	{
		return Kind != EStorageKind::Crate && Kind != EStorageKind::Barrel;
	}

	bool IsHeavy(EStorageKind Kind)
	{
		return Kind == EStorageKind::Barrel || Kind == EStorageKind::Shelf || Kind == EStorageKind::Table || Kind == EStorageKind::Cauldron
			|| Kind == EStorageKind::DryingRack || Kind == EStorageKind::Workbench;
	}

	float Height(EStorageKind Kind)
	{
		switch (Kind)
		{
		case EStorageKind::Barrel:   return 95.f;
		case EStorageKind::Shelf:    return ErewhonApothecary::ShelfHeight;
		case EStorageKind::Table:    return 82.f;
		case EStorageKind::Mortar:   return 20.f;
		case EStorageKind::Cauldron: return 48.f;
		case EStorageKind::DryingRack: return 170.f;
		case EStorageKind::Workbench:  return 120.f;
		case EStorageKind::Potpourri:  return 14.f;
		case EStorageKind::CauldronStand: return 150.f;
		case EStorageKind::FirewoodRack:  return 110.f;
		case EStorageKind::Saddlebag:     return 52.f;
		case EStorageKind::LogPile:       return 70.f;
		default:                     return 36.f;
		}
	}

	FText KindName(EStorageKind Kind)
	{
		switch (Kind)
		{
		case EStorageKind::Barrel:   return LOCTEXT("Barrel", "Tonneau");
		case EStorageKind::Shelf:    return LOCTEXT("Shelf", "Étagère");
		case EStorageKind::Table:    return LOCTEXT("Table", "Table");
		case EStorageKind::Mortar:   return LOCTEXT("Mortar", "Mortier");
		case EStorageKind::Cauldron: return LOCTEXT("Cauldron", "Chaudron");
		case EStorageKind::DryingRack: return LOCTEXT("DryingRack", "Séchoir");
		case EStorageKind::Workbench:  return LOCTEXT("Workbench", "Atelier");
		case EStorageKind::Potpourri:  return LOCTEXT("Potpourri", "Pot-pourri");
		case EStorageKind::CauldronStand: return LOCTEXT("CauldronStand", "Support de chaudron");
		case EStorageKind::FirewoodRack:  return LOCTEXT("FirewoodRack", "Rack à petit bois");
		case EStorageKind::Saddlebag:     return LOCTEXT("Saddlebag", "Sacoche");
		case EStorageKind::LogPile:       return LOCTEXT("LogPile", "Tas de bûches");
		default:                     return LOCTEXT("Crate", "Cagette");
		}
	}

	bool CanPlaceAt(const UObject* WorldContext, const FString& PlayerKey, bool bCart, EStorageKind Kind, uint8 Type, const FVector& Location)
	{
		if (bCart)
		{
			return true;
		}
		if (Type == 2)
		{
			return !IsFurniture(Kind);
		}
		if (Type == 0 && FountainSnap(WorldContext, Kind, Location).IsSet())
		{
			return true;   // sous le bec de la fontaine, chacun peut poser son tonneau
		}
		const AErewhonGameState* State = AErewhonGameState::Get(WorldContext);
		const FString* Owner = State ? State->FindSectorOwnerKey(ErewhonSector::FromWorld(Location)) : nullptr;
		return Owner && !PlayerKey.IsEmpty() && *Owner == PlayerKey;
	}

	TOptional<FVector> FountainSnap(const UObject* WorldContext, EStorageKind Kind, const FVector& Location)
	{
		if (Kind != EStorageKind::Barrel)
		{
			return {};
		}
		const FVector Spot = ErewhonVillage::FountainBarrelSpot();
		if (FVector::Dist2D(Spot, Location) > ErewhonVillage::FountainSnapRadius)
		{
			return {};
		}
		const AErewhonGameState* State = AErewhonGameState::Get(WorldContext);
		return FVector(Spot.X, Spot.Y, State ? ErewhonTerrain::Height(FVector2D(Spot), State->GetWorldSeed()) : Location.Z);
	}

	TOptional<FTransform> HookFor(const UObject* WorldContext, EStorageKind Kind, const FVector& Location)
	{
		// Le support se pose dans la cheminée ; le chaudron pend au support, à sa hauteur.
		const FIntPoint Sector = ErewhonSector::FromWorld(Location);
		const TOptional<FTransform> Spot = ErewhonApothecary::StandSpot(WorldContext, Sector);
		if (!Spot.IsSet() || FVector::Dist2D(Spot->GetLocation(), Location) > 170.f)
		{
			return {};
		}
		if (Kind == EStorageKind::CauldronStand)
		{
			return ErewhonApothecary::FindStand(WorldContext, Sector) ? TOptional<FTransform>() : Spot;
		}
		if (Kind == EStorageKind::Cauldron)
		{
			if (const AStorageActor* Stand = ErewhonApothecary::FindStand(WorldContext, Sector))
			{
				return ErewhonApothecary::CauldronSpotOn(Stand);
			}
		}
		return {};
	}

	UStaticMeshComponent* AddBundle(AActor* Owner, USceneComponent* Parent, FName ItemId, const FTransform& At, float Length, bool bLying)
	{
		FString Plant = ItemId.ToString();
		const bool bDried = Plant.RemoveFromStart(TEXT("dried_"));
		UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bundle_") + Plant);
		if (!Mesh)
		{
			return nullptr;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(Mesh);
		Part->SetupAttachment(Parent);
		// Séché : plus fin (en travers), et de la couleur de la paille.
		const FTransform Thin(FRotator::ZeroRotator, FVector::ZeroVector, bDried ? FVector(0.75f, 1.f, 0.75f) : FVector::OneVector);
		FTransform Fit = FitToLength(Mesh, Length);
		const FBox Bounds = Mesh->GetBoundingBox();
		const FVector Size = Bounds.GetSize();
		if (bLying && Size.Z > FMath::Max(Size.X, Size.Y))
		{
			// Une plante haute (la fougère) : couchée, sa hauteur dans la longueur (Y), posée à plat.
			const float Scale = Length / FMath::Max(1.f, static_cast<float>(Size.Z));
			Fit = FTransform(-Bounds.GetCenter()) * FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(Scale))
				* FTransform(FRotator(0.f, 0.f, 90.f), FVector(0.f, 0.f, Size.Y * Scale * 0.5f));
		}
		Part->SetRelativeTransform(Fit * Thin * At);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		if (bDried)
		{
			UTexture* Texture = nullptr;
			UMaterialInterface* Dried = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_DriedMesh"));
			if (Dried && Mesh->GetMaterial(0) && Mesh->GetMaterial(0)->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), Texture))
			{
				UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Dried, Part);
				Tint->SetTextureParameterValue(TEXT("BaseColor"), Texture);
				Part->SetMaterial(0, Tint);
			}
		}
		Part->RegisterComponent();
		Owner->AddInstanceComponent(Part);
		return Part;
	}

	UStaticMesh* StorageMesh(EStorageKind Kind, FTransform& OutFit)
	{
		// La cagette, l'étagère et la table sont construites en planches.
		if (Kind == EStorageKind::Crate || Kind == EStorageKind::Shelf || Kind == EStorageKind::Table || Kind == EStorageKind::LogPile)
		{
			return nullptr;
		}
		const TCHAR* ModelName = Kind == EStorageKind::Mortar ? TEXT("SM_mortar") : Kind == EStorageKind::Cauldron ? TEXT("SM_cauldron")
			: Kind == EStorageKind::DryingRack ? TEXT("SM_drying_rack") : Kind == EStorageKind::Workbench ? TEXT("SM_workbench")
			: Kind == EStorageKind::Potpourri ? TEXT("SM_potpourri")
			: Kind == EStorageKind::FirewoodRack ? (LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_firewood_rack_empty")) ? TEXT("SM_firewood_rack_empty") : TEXT("SM_firewood_rack"))
			: Kind == EStorageKind::CauldronStand ? TEXT("SM_cauldron_stand") : Kind == EStorageKind::Saddlebag ? TEXT("SM_saddlebag") : nullptr;
		if (ModelName)
		{
			UStaticMesh* Model = LoadGenerated<UStaticMesh>(TEXT("Meshes"), ModelName);
			if (Model)
			{
				// Le séchoir et l'atelier : largeur imposée, façade sur X une fois tournés (voir BeginPlay).
				OutFit = Kind == EStorageKind::DryingRack ? FitToBox(Model, 150.f, 60.f, Height(Kind))
					: Kind == EStorageKind::Workbench ? FitToBox(Model, 160.f, 70.f, Height(Kind))
					: Kind == EStorageKind::Potpourri ? FitToLength(Model, 28.f, Height(Kind))
					: Kind == EStorageKind::FirewoodRack ? FitToBox(Model, 110.f, 45.f, Height(Kind))
					// La potence : son mât au fond (+X), le bout de sa potence au-dessus du centre, là où pend le chaudron.
					: Kind == EStorageKind::CauldronStand ? FitToHeight(Model, Height(Kind)) * FTransform(FRotator(0.f, 90.f, 0.f), FVector(32.f, 0.f, 0.f))
					: FitToHeight(Model, Height(Kind));
			}
			return Model;
		}
		UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_barrel"));
		if (Mesh)
		{
			OutFit = Kind == EStorageKind::Barrel ? FitToHeight(Mesh, Height(Kind)) : FitToLength(Mesh, 60.f, Height(Kind));
		}
		return Mesh;
	}

	UStaticMesh* ImplementMesh(ECartKind Kind, FTransform& OutFit)
	{
		if (Kind == ECartKind::Cart)
		{
			return CartMesh(OutFit);
		}
		// Le grand côté (les brancards) sur Y une fois mis à la longueur : on le couche sur X, brancards vers −X.
		UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), Kind == ECartKind::Cultivator ? TEXT("SM_cultivator") : Kind == ECartKind::Harvester ? TEXT("SM_harvester") : TEXT("SM_seeder"));
		if (Mesh)
		{
			const float Length = Kind == ECartKind::Cultivator ? 300.f : Kind == ECartKind::Harvester ? 340.f : 320.f;
			const float Yaw = Kind == ECartKind::Cultivator ? GCultivatorYaw : Kind == ECartKind::Harvester ? GHarvesterYaw : GSeederYaw;
			const float Shift = Kind == ECartKind::Cultivator ? -40.f : Kind == ECartKind::Harvester ? 60.f : 70.f;
			OutFit = FitToLength(Mesh, Length) * FTransform(FRotator(0.f, Yaw, 0.f), FVector(Shift, 0.f, 0.f));
		}
		return Mesh;
	}

	TOptional<ECartKind> CartKindForItem(FName ItemId)
	{
		if (ItemId == TEXT("cart")) { return ECartKind::Cart; }
		if (ItemId == TEXT("cultivator")) { return ECartKind::Cultivator; }
		if (ItemId == TEXT("seeder")) { return ECartKind::Seeder; }
		if (ItemId == TEXT("harvester")) { return ECartKind::Harvester; }
		return {};
	}

	FName ItemForCartKind(ECartKind Kind)
	{
		return Kind == ECartKind::Cultivator ? FName(TEXT("cultivator")) : Kind == ECartKind::Seeder ? FName(TEXT("seeder"))
			: Kind == ECartKind::Harvester ? FName(TEXT("harvester")) : FName(TEXT("cart"));
	}

	UStaticMesh* CartMesh(FTransform& OutFit)
	{
		// La caisse sans roues d'abord ; à défaut, l'ancien modèle d'un seul tenant.
		if (UStaticMesh* Body = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cart_body")))
		{
			OutFit = FitToLength(Body, GBodyLength) * FTransform(FRotator(0.f, GCartBodyYaw, 0.f), GBodyOffset);
			return Body;
		}
		UStaticMesh* Mesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cart"));
		if (Mesh)
		{
			OutFit = FitToLength(Mesh, GCartLength) * FTransform(FRotator(0.f, GCartMeshYaw, 0.f), FVector(-20.f, 0.f, 0.f));
		}
		return Mesh;
	}
}

// ── Conteneurs ───────────────────────────────────────────────────────────────

AStorageActor::AStorageActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	bReplicates = true;
	SetReplicatingMovement(true);   // les attaches (mains, pile, charrette) suivent sur les clients
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AStorageActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AStorageActor, Kind);
	DOREPLIFETIME(AStorageActor, OwnerKey);
	DOREPLIFETIME(AStorageActor, OwnerName);
	DOREPLIFETIME(AStorageActor, Slots);
	DOREPLIFETIME(AStorageActor, Carrier);
	DOREPLIFETIME(AStorageActor, Cart);
	DOREPLIFETIME(AStorageActor, CartSlot);
	DOREPLIFETIME(AStorageActor, Below);
	DOREPLIFETIME(AStorageActor, Above);
	DOREPLIFETIME(AStorageActor, SlotTimes);
	DOREPLIFETIME(AStorageActor, ExpiresAt);
	DOREPLIFETIME(AStorageActor, Water);
	DOREPLIFETIME(AStorageActor, Liquid);
	DOREPLIFETIME(AStorageActor, Temperature);
	DOREPLIFETIME(AStorageActor, BrewProgress);
	DOREPLIFETIME(AStorageActor, Level);
}

void AStorageActor::Setup(EStorageKind InKind, const FString& InOwnerKey, const FString& InOwnerName)
{
	Kind = InKind;
	OwnerKey = InOwnerKey;
	OwnerName = InOwnerName;
	Slots.SetNum(ErewhonPlaceables::Capacity(Kind));
	SlotTimes.SetNum(Slots.Num());
}

void AStorageActor::BeginPlay()
{
	Super::BeginPlay();
	FTransform Fit;
	if (UStaticMesh* Mesh = ErewhonPlaceables::StorageMesh(Kind, Fit))
	{
		Body = NewObject<UStaticMeshComponent>(this);
		Body->SetStaticMesh(Mesh);
		Body->SetupAttachment(Root);
		Body->SetRelativeTransform(Fit);
		Body->RegisterComponent();
		AddInstanceComponent(Body);
		if (Kind == EStorageKind::CauldronStand)
		{
			// Le modèle de la potence : on vise (et on reprend) par son mât, on vise le chaudron au travers.
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			const float H = ErewhonPlaceables::Height(EStorageKind::CauldronStand);
			Chain = AddShape(this, Root, EShape::Cylinder, FVector::ZeroVector, FVector(2.f, 2.f, 100.f), FLinearColor::FromSRGBColor(FColor(44, 42, 40)), FRotator::ZeroRotator, false);
			OnRep_Level();
			UStaticMeshComponent* Hull = AddShape(this, Root, EShape::Cube, FVector(50.f, 0.f, H * 0.6f), FVector(16.f, 16.f, H * 0.8f), FLinearColor::Black, FRotator::ZeroRotator, true);
			Hull->SetHiddenInGame(true);
			Body = Hull;
		}
	}
	else if (Kind == EStorageKind::CauldronStand)
	{
		// Une potence de fer forgé : trois pieds, un mât, une potence et sa chaîne, qui monte ou descend.
		const FLinearColor Iron = FLinearColor::FromSRGBColor(FColor(44, 42, 40));
		const float H = ErewhonPlaceables::Height(EStorageKind::CauldronStand);
		for (int32 i = 0; i < 3; ++i)
		{
			const float Angle = 120.f * i + 90.f;
			const FVector Out = FRotator(0.f, Angle, 0.f).Vector() * 38.f;
			AddShape(this, Root, EShape::Cylinder, FVector(Out.X * 0.5f, Out.Y * 0.5f, 18.f), FVector(4.f, 4.f, 44.f), Iron,
				FRotator(0.f, Angle, 0.f) + FRotator(-50.f, 0.f, 0.f), false);
		}
		AddShape(this, Root, EShape::Cylinder, FVector(34.f, 0.f, H * 0.5f), FVector(5.f, 5.f, H), Iron, FRotator::ZeroRotator, false);
		AddShape(this, Root, EShape::Cylinder, FVector(17.f, 0.f, H - 6.f), FVector(4.f, 4.f, 40.f), Iron, FRotator(90.f, 0.f, 0.f), false);
		for (int32 Tooth = 0; Tooth < 8; ++Tooth)
		{
			AddShape(this, Root, EShape::Cube, FVector(30.f, 0.f, 40.f + Tooth * 9.f), FVector(4.f, 3.f, 2.f), Iron, FRotator::ZeroRotator, false);   // la crémaillère
		}
		Chain = AddShape(this, Root, EShape::Cylinder, FVector::ZeroVector, FVector(2.f, 2.f, 100.f), Iron, FRotator::ZeroRotator, false);
		OnRep_Level();
		// On le vise (et on le reprend) par son mât.
		UStaticMeshComponent* Hull = AddShape(this, Root, EShape::Cube, FVector(34.f, 0.f, H * 0.6f), FVector(16.f, 16.f, H * 0.8f), Iron, FRotator::ZeroRotator, true);
		Hull->SetHiddenInGame(true);
		Body = Hull;
	}
	else if (Kind == EStorageKind::Shelf || Kind == EStorageKind::Table)
	{
		// Le meuble en planches, et une boîte invisible pour sa collision (ciblage, pose).
		const FVector Size = Kind == EStorageKind::Shelf
			? FVector(ErewhonApothecary::ShelfDepth, ErewhonApothecary::ShelfWidth, ErewhonApothecary::ShelfHeight)
			: FVector(84.f, 190.f, 82.f);
		if (Kind == EStorageKind::Shelf)
		{
			DisplayFrame = NewObject<USceneComponent>(this);
			DisplayFrame->SetupAttachment(Root);
			DisplayFrame->RegisterComponent();
			AddInstanceComponent(DisplayFrame);
			ErewhonApothecary::BuildShelfFrame(this, DisplayFrame);
		}
		else
		{
			UMaterialInterface* Wood = MakePaintedMaterial(this, TEXT("wood_planks"), 120.f, Palette::WoodLight);
			AddShape(this, Root, EShape::Cube, FVector(0, 0, 78.f), FVector(84.f, 190.f, 8.f), Palette::WoodLight, FRotator::ZeroRotator, false)->SetMaterial(0, Wood);
			for (const float SX : { -1.f, 1.f })
			{
				for (const float SY : { -1.f, 1.f })
				{
					AddShape(this, Root, EShape::Cube, FVector(SX * 34.f, SY * 85.f, 37.f), FVector(8.f, 8.f, 74.f), Palette::WoodDark, FRotator::ZeroRotator, false);
				}
			}
		}
		UStaticMeshComponent* Hull = AddShape(this, Root, EShape::Cube, FVector(0, 0, Size.Z * 0.5f), Size, Palette::WoodLight, FRotator::ZeroRotator, true);
		Hull->SetHiddenInGame(true);
		Body = Hull;
	}
	else if (Kind == EStorageKind::Barrel)
	{
		Body = AddShape(this, Root, EShape::Cylinder, FVector(0, 0, 47.5f), FVector(60, 60, 95), Palette::WoodLight);
		Body->SetMaterial(0, MakePaintedMaterial(Body, TEXT("wood_planks"), 80.f, Palette::WoodLight));
	}
	else if (Kind == EStorageKind::Crate)
	{
		// Cagette ouverte : un fond, trois lattes par côté avec des jours entre elles, des montants
		// aux coins et une poignée découpée sur les petits côtés. 60 × 40 × 36 cm.
		const FLinearColor Slat = FLinearColor::FromSRGBColor(FColor(214, 178, 128));
		UMaterialInterface* Wood = MakePaintedMaterial(this, TEXT("wood_planks"), 60.f, Slat, FLinearColor(1.15f, 1.1f, 1.f));
		const auto Plank = [this, Wood, &Slat](const FVector& Center, const FVector& Size, bool bSolid)
		{
			UStaticMeshComponent* Part = AddShape(this, Root, EShape::Cube, Center, Size, Slat, FRotator::ZeroRotator, bSolid);
			Part->SetMaterial(0, Wood);
			return Part;
		};
		constexpr float L = 60.f, W = 40.f, H = 36.f, T = 1.6f;
		Plank(FVector(0, 0, 1.f), FVector(L, W, 2.f), false);   // le fond
		for (const float Z : { 6.f, 17.f, 28.f })
		{
			for (const float Side : { -1.f, 1.f })
			{
				Plank(FVector(0, Side * (W * 0.5f - T * 0.5f), Z), FVector(L, T, 8.f), false);              // grands côtés
				if (Z < 25.f)
				{
					Plank(FVector(Side * (L * 0.5f - T * 0.5f), 0, Z), FVector(T, W, 8.f), false);          // petits côtés
				}
			}
		}
		for (const float Side : { -1.f, 1.f })
		{
			// La latte du haut des petits côtés : deux morceaux autour de la poignée.
			for (const float Half : { -1.f, 1.f })
			{
				Plank(FVector(Side * (L * 0.5f - T * 0.5f), Half * 13.5f, 30.f), FVector(T, 13.f, 12.f), false);
			}
			for (const float Corner : { -1.f, 1.f })
			{
				Plank(FVector(Side * (L * 0.5f - 2.5f), Corner * (W * 0.5f - 2.5f), H * 0.5f), FVector(4.f, 4.f, H), false);
			}
		}
		// Une boîte invisible donne au tout sa collision (ciblage, empilement).
		UStaticMeshComponent* Hull = AddShape(this, Root, EShape::Cube, FVector(0, 0, H * 0.5f), FVector(L, W, H), Slat, FRotator::ZeroRotator, true);
		Hull->SetHiddenInGame(true);
		Body = Hull;
	}
	else if (Kind == EStorageKind::LogPile)
	{
		// Une boîte invisible pour la visée et la collision ; les bûches se dessinent selon le nombre.
		UStaticMeshComponent* Hull = AddShape(this, Root, EShape::Cube, FVector(0, 0, 35.f), FVector(130.f, 215.f, 70.f), Palette::WoodDark, FRotator::ZeroRotator, true);
		Hull->SetHiddenInGame(true);
		Body = Hull;
		DisplayFrame = NewObject<USceneComponent>(this);
		DisplayFrame->SetupAttachment(Root);
		DisplayFrame->RegisterComponent();
		AddInstanceComponent(DisplayFrame);
		RefreshDisplay();
	}
	if (Kind == EStorageKind::DryingRack && !DisplayFrame)
	{
		DisplayFrame = NewObject<USceneComponent>(this);
		DisplayFrame->SetupAttachment(Root);
		DisplayFrame->RegisterComponent();
		AddInstanceComponent(DisplayFrame);
		if (!Body)
		{
			// Sans modèle : deux montants et trois barres.
			for (const float SY : { -1.f, 1.f })
			{
				AddShape(this, Root, EShape::Cube, FVector(0.f, SY * 70.f, 85.f), FVector(8.f, 8.f, 170.f), Palette::WoodDark, FRotator::ZeroRotator, false);
			}
			for (int32 Bar = 0; Bar < 3; ++Bar)
			{
				AddShape(this, Root, EShape::Cylinder, FVector(0.f, 0.f, 170.f * (0.9f - 0.2f * Bar)), FVector(4.f, 4.f, 140.f), Palette::WoodLight, FRotator(0.f, 0.f, 90.f), false);
			}
		}
	}
	if (!Body && (Kind == EStorageKind::DryingRack || Kind == EStorageKind::Workbench || Kind == EStorageKind::Potpourri))
	{
		// Sans modèle généré : une boîte (cible, collision), visible pour l'atelier et le pot-pourri.
		const FVector Size = Kind == EStorageKind::DryingRack ? FVector(50.f, 150.f, 170.f) : Kind == EStorageKind::Workbench ? FVector(70.f, 160.f, 120.f) : FVector(28.f, 28.f, 14.f);
		Body = AddShape(this, Root, EShape::Cube, FVector(0, 0, Size.Z * 0.5f), Size, Palette::WoodLight, FRotator::ZeroRotator, true);
		Body->SetHiddenInGame(Kind == EStorageKind::DryingRack);
	}
	ApplyCollision();
	RefreshDisplay();
	SetActorTickEnabled(Kind == EStorageKind::DryingRack || Kind == EStorageKind::Potpourri || Kind == EStorageKind::Cauldron);

	// Le pot-pourri : quelques lueurs de pétales, roses et lilas, qui flottent autour de lui.
	if (Kind == EStorageKind::Potpourri && GetNetMode() != NM_DedicatedServer)
	{
		UMaterialInterface* GlowMaterial = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Glow"));
		UTexture* Dot = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_star"));
		const FLinearColor Colors[] = { FLinearColor(1.f, 0.55f, 0.75f), FLinearColor(0.75f, 0.6f, 1.f), FLinearColor(1.f, 0.85f, 0.6f) };
		for (int32 i = 0; i < 9 && GlowMaterial && Dot; ++i)
		{
			USceneComponent* Petal = NewObject<USceneComponent>(this);
			Petal->SetupAttachment(Root);
			Petal->RegisterComponent();
			AddInstanceComponent(Petal);
			for (const float Yaw : { 0.f, 90.f })
			{
				if (UProceduralMeshComponent* Quad = AddPicture(this, Petal, FVector::ZeroVector, 20.f, 20.f, Yaw, Dot))
				{
					UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(GlowMaterial, this);
					Glow->SetVectorParameterValue(TEXT("Color"), Colors[i % 3]);
					Glow->SetScalarParameterValue(TEXT("Seed"), i * 0.9f);
					Glow->SetScalarParameterValue(TEXT("Intensity"), 12.f);
					Quad->SetMaterial(0, Glow);
					Quad->SetCastShadow(false);
				}
			}
			Petals.Add(Petal);
		}
	}
}

float AStorageActor::GetCapacityLiters() const
{
	return Kind == EStorageKind::Cauldron ? ErewhonPlaceables::CauldronLiters : Kind == EStorageKind::Barrel ? ErewhonPlaceables::WaterBarrelLiters : 0.f;
}

void AStorageActor::SetWater(float Liters)
{
	Water = FMath::Clamp(Liters, 0.f, GetCapacityLiters());
	if (Water <= 0.01f)
	{
		Water = 0.f;
		Liquid = ErewhonApothecary::Water;   // vide : prêt pour n'importe quel liquide
	}
	ForceNetUpdate();
	RefreshDisplay();
	OnStorageChanged.Broadcast();
}

void AStorageActor::SetLiquid(FName InLiquid, float Liters)
{
	Liquid = InLiquid.IsNone() ? ErewhonApothecary::Water : InLiquid;
	SetWater(Liters);
}

void AStorageActor::SetLevel(float InLevel)
{
	Level = FMath::Clamp(InLevel, 0.f, 1.f);
	ForceNetUpdate();
	OnRep_Level();
}

void AStorageActor::OnRep_Level()
{
	// La chaîne du support : du haut de la potence jusqu'à l'anse du chaudron.
	if (Chain)
	{
		const float Top = ErewhonPlaceables::Height(EStorageKind::CauldronStand) - 12.f;
		const float Bottom = 30.f + 45.f * Level + ErewhonPlaceables::Height(EStorageKind::Cauldron) - 4.f;
		const float Length = FMath::Max(4.f, Top - Bottom);
		Chain->SetRelativeLocation(FVector(0.f, 0.f, Bottom + Length * 0.5f));
		Chain->SetRelativeScale3D(FVector(0.02f, 0.02f, Length / 100.f));
	}
	OnStorageChanged.Broadcast();
}

void AStorageActor::UpdateBrewing(float DeltaSeconds)
{
	using namespace ErewhonApothecary;
	AStorageActor* Stand = nullptr;
	const AApothecary* House = HouseOfHungCauldron(this, &Stand);
	// Sur son support, au-dessus d'un feu allumé : la température tend vers ce que donne la hauteur ; sinon, vers celle de la pièce.
	const float Target = House && House->IsFireLit() && Stand ? FMath::Lerp(HottestTemp, CoolestTemp, Stand->GetLevel()) : RoomTemp;
	const float Rate = Target > Temperature ? 1.6f : 0.45f;   // °C par seconde : on chauffe vite, on refroidit doucement
	Temperature = FMath::FInterpConstantTo(Temperature, Target, DeltaSeconds, Rate);

	// Le breuvage : de l'eau, les bons ingrédients dans la bonne proportion, le temps qu'il faut à la bonne température.
	// La base se fait dans l'eau ; les autres breuvages, dans la base.
	const FBrew* Brew = MatchBrew(Slots, Water);
	if (Brew && Liquid != SolventOf(*Brew))
	{
		Brew = nullptr;
	}
	if (Brew && Temperature >= Brew->MinTemp && Temperature <= Brew->MaxTemp)
	{
		BrewProgress += DeltaSeconds;
		if (BrewProgress >= Brew->Seconds)
		{
			Liquid = Brew->Liquid;
			for (FInventorySlot& Slot : Slots)
			{
				Slot.Clear();
			}
			BrewProgress = 0.f;
			Commit();
			ForceNetUpdate();
		}
	}
	else
	{
		BrewProgress = FMath::Max(0.f, BrewProgress - DeltaSeconds * 0.5f);   // hors de la fourchette, ça retombe
	}
	NetTimer += DeltaSeconds;
	if (NetTimer > 0.5f)
	{
		NetTimer = 0.f;
		ForceNetUpdate();
	}
}

void AStorageActor::Commit()
{
	if (HasAuthority())
	{
		UpdateDrying();
	}
	RefreshDisplay();
	OnStorageChanged.Broadcast();
}

void AStorageActor::RefreshDisplay()
{
	if (Kind == EStorageKind::Cauldron && GetNetMode() != NM_DedicatedServer)
	{
		// La surface du liquide, à sa couleur, plus haute quand le chaudron est plein.
		if (!Surface)
		{
			Surface = AddShape(this, Root, EShape::Cylinder, FVector::ZeroVector, FVector(30.f, 30.f, 1.f), FLinearColor::White, FRotator::ZeroRotator, false);
			Surface->SetCastShadow(false);
		}
		const float Fill = Water / FMath::Max(1.f, GetCapacityLiters());
		Surface->SetVisibility(Water > 0.01f);
		Surface->SetRelativeLocation(FVector(0.f, 0.f, ErewhonPlaceables::Height(EStorageKind::Cauldron) * (0.45f + 0.4f * Fill)));
		Surface->SetRelativeScale3D(FVector(0.26f + 0.08f * Fill, 0.26f + 0.08f * Fill, 0.01f));
		Surface->SetMaterial(0, MakeColorMaterial(Surface, ErewhonApothecary::LiquidColor(Liquid)));
	}
	if (Kind == EStorageKind::Shelf && DisplayFrame)
	{
		ErewhonApothecary::BuildShelfDisplay(this, DisplayFrame, Slots, DisplayParts);
	}
	else if ((Kind == EStorageKind::Crate || Kind == EStorageKind::Barrel) && GetNetMode() != NM_DedicatedServer)
	{
		// Ce qu'on a récolté se voit : quelques bouquets couchés dans la cagette, sur le dessus du tonneau.
		if (!DisplayFrame)
		{
			DisplayFrame = NewObject<USceneComponent>(this);
			DisplayFrame->SetupAttachment(Root);
			DisplayFrame->RegisterComponent();
			AddInstanceComponent(DisplayFrame);
		}
		for (UPrimitiveComponent* Part : DisplayParts)
		{
			if (Part)
			{
				Part->DestroyComponent();
			}
		}
		DisplayParts.Reset();
		const bool bCrate = Kind == EStorageKind::Crate;
		int32 Shown = 0;
		for (const FInventorySlot& Slot : Slots)
		{
			if (Slot.IsEmpty() || Shown >= (bCrate ? 6 : 3))
			{
				continue;
			}
			// Deux rangs dans la cagette (couchés en long), quelques-uns en étoile sur le tonneau.
			const FTransform At = bCrate
				? FTransform(FRotator(0.f, 90.f + (Shown % 2 ? 6.f : -6.f), 0.f), FVector(-14.f + 14.f * (Shown % 3), (Shown < 3 ? -8.f : 8.f), 8.f + 6.f * (Shown / 3)))
				: FTransform(FRotator(0.f, Shown * 60.f, 0.f), FVector(0.f, 0.f, ErewhonPlaceables::Height(EStorageKind::Barrel) + 2.f));
			if (UStaticMeshComponent* Bundle = ErewhonPlaceables::AddBundle(this, DisplayFrame, Slot.ItemId, At, bCrate ? 46.f : 40.f, true))
			{
				DisplayParts.Add(Bundle);
				++Shown;
			}
		}
	}
	else if (Kind == EStorageKind::LogPile && DisplayFrame && GetNetMode() != NM_DedicatedServer)
	{
		// Deux pyramides de bûches couchées côte à côte (4, 3, 2, 1), qui montent avec le nombre.
		for (UPrimitiveComponent* Part : DisplayParts)
		{
			if (Part)
			{
				Part->DestroyComponent();
			}
		}
		DisplayParts.Reset();
		UStaticMesh* LogMesh = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_log"));
		const int32 Count = FMath::Min(CountStored(ErewhonForestry::Log), ErewhonForestry::PileCapacity);
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 InStack = i % 10;
			const int32 Row = InStack < 4 ? 0 : InStack < 7 ? 1 : InStack < 9 ? 2 : 3;
			const int32 First = Row == 0 ? 0 : Row == 1 ? 4 : Row == 2 ? 7 : 9;
			const int32 InRow = 4 - Row;
			const float X = ((InStack - First) - (InRow - 1) * 0.5f) * 30.f;
			const FVector At(X, i < 10 ? -52.f : 52.f, 15.f + Row * 25.f);
			const FRotator Turn(0.f, (i * 37 % 9) - 4.f, 0.f);   // un peu de désordre
			if (LogMesh)
			{
				UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
				Part->SetStaticMesh(LogMesh);
				Part->SetupAttachment(DisplayFrame);
				Part->SetRelativeTransform(FitToLength(LogMesh, 100.f, 30.f) * FTransform(Turn, At - FVector(0, 0, 15.f)));
				Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Part->RegisterComponent();
				AddInstanceComponent(Part);
				DisplayParts.Add(Part);
			}
			else
			{
				DisplayParts.Add(AddShape(this, DisplayFrame, EShape::Cylinder, At, FVector(28.f, 28.f, 100.f), FLinearColor::FromSRGBColor(FColor(120, 86, 58)), FRotator(0.f, Turn.Yaw, 90.f), false));
			}
		}
	}
	else if (Kind == EStorageKind::FirewoodRack && GetNetMode() != NM_DedicatedServer)
	{
		// Des piles de bûchettes sur les deux étagères, une pile toutes les cinq brassées.
		if (!DisplayFrame)
		{
			DisplayFrame = NewObject<USceneComponent>(this);
			DisplayFrame->SetupAttachment(Root);
			DisplayFrame->RegisterComponent();
			AddInstanceComponent(DisplayFrame);
		}
		for (UPrimitiveComponent* Part : DisplayParts)
		{
			if (Part)
			{
				Part->DestroyComponent();
			}
		}
		DisplayParts.Reset();
		UStaticMesh* Logs = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_firewood"));
		const int32 Piles = FMath::Min(8, (CountStored(TEXT("firewood")) + 4) / 5);
		for (int32 i = 0; i < Piles && Logs; ++i)
		{
			const FVector At(0.f, -39.f + 26.f * (i % 4), i < 4 ? 14.f : 58.f);
			UStaticMeshComponent* Pile = NewObject<UStaticMeshComponent>(this);
			Pile->SetStaticMesh(Logs);
			Pile->SetupAttachment(DisplayFrame);
			Pile->SetRelativeTransform(FitToLength(Logs, 30.f, 22.f) * FTransform(FRotator(0.f, 90.f, 0.f), At));
			Pile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pile->RegisterComponent();
			AddInstanceComponent(Pile);
			DisplayParts.Add(Pile);
		}
	}
	else if (Kind == EStorageKind::DryingRack && DisplayFrame && GetNetMode() != NM_DedicatedServer)
	{
		// Des bouquets pendus tête en bas aux barres du séchoir : un, deux ou trois selon la quantité.
		for (UPrimitiveComponent* Part : DisplayParts)
		{
			if (Part)
			{
				Part->DestroyComponent();
			}
		}
		DisplayParts.Reset();
		const UItemDatabase* Items = UItemDatabase::Get(this);
		for (int32 i = 0; i < Slots.Num() && Items; ++i)
		{
			const FItemDef* Def = Slots[i].IsEmpty() ? nullptr : Items->Find(Slots[i].ItemId);
			if (!Def)
			{
				continue;
			}
			const float BarZ = ErewhonPlaceables::Height(EStorageKind::DryingRack) * (0.9f - 0.2f * (i / 2));   // les trois barres du haut
			const float SideY = (i % 2 ? 1.f : -1.f) * 38.f;
			const int32 Bunches = Slots[i].Count >= 8 ? 3 : Slots[i].Count >= 4 ? 2 : 1;
			for (int32 b = 0; b < Bunches; ++b)
			{
				const FVector Hook(0.f, SideY + (b - (Bunches - 1) * 0.5f) * 15.f, BarZ);
				DisplayParts.Add(AddShape(this, DisplayFrame, EShape::Cylinder, Hook - FVector(0, 0, 4.f), FVector(1.f, 1.f, 8.f), Palette::WoodDark, FRotator::ZeroRotator, false));
				// Le bouquet, tête en bas : son modèle (pâli s'il a séché), à défaut un cône à sa couleur.
				const FTransform Hang(FRotator(0.f, 90.f + b * 25.f, 90.f), Hook - FVector(0, 0, 20.f));
				if (UStaticMeshComponent* Bundle = ErewhonPlaceables::AddBundle(this, DisplayFrame, Def->Id, Hang, 32.f))
				{
					DisplayParts.Add(Bundle);
				}
				else
				{
					DisplayParts.Add(AddShape(this, DisplayFrame, EShape::Cone, Hook - FVector(0, 0, 8.f), FVector(13.f, 13.f, 26.f), Def->Color, FRotator(180.f, 0.f, 0.f), false));
				}
			}
		}
	}
}

int32 AStorageActor::SlotCap(const FItemDef& Def) const
{
	return Kind == EStorageKind::DryingRack || Kind == EStorageKind::FirewoodRack ? FMath::Min(Def.MaxStack, ErewhonApothecary::PotionsPerSlot) : Def.MaxStack;
}

void AStorageActor::ApplyCollision()
{
	// Porté, il ne doit pas bousculer son porteur.
	if (Body)
	{
		Body->SetCollisionProfileName(Carrier ? UCollisionProfile::NoCollision_ProfileName : UCollisionProfile::BlockAll_ProfileName);
	}
	// Un meuble ne se porte pas à bout de bras : il disparaît le temps qu'on choisisse où le poser (l'aperçu le montre).
	SetActorHiddenInGame(Carrier && ErewhonPlaceables::IsFurniture(Kind));
}

void AStorageActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Kind == EStorageKind::DryingRack && HasAuthority())
	{
		UpdateDrying();
	}
	else if (Kind == EStorageKind::Cauldron && HasAuthority())
	{
		UpdateBrewing(DeltaSeconds);
	}
	else if (Kind == EStorageKind::Potpourri)
	{
		UpdatePotpourri(DeltaSeconds);
	}
}

void AStorageActor::UpdateDrying()
{
	if (Kind != EStorageKind::DryingRack)
	{
		return;
	}
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const UItemDatabase* Items = UItemDatabase::Get(this);
	if (!State || !Items)
	{
		return;
	}
	const double Now = State->GetWorldClock();
	SlotTimes.SetNum(Slots.Num());
	TimedItems.SetNum(Slots.Num());
	bool bChanged = false;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FInventorySlot& Slot = Slots[i];
		const FItemDef* Def = Slot.IsEmpty() ? nullptr : Items->Find(Slot.ItemId);
		if (!Def || Def->Category != EItemCategory::Plant)
		{
			// Vide, ou déjà sec : rien ne court.
			bChanged |= SlotTimes[i] != 0.0;
			SlotTimes[i] = 0.0;
			TimedItems[i] = NAME_None;
			continue;
		}
		if (TimedItems[i] != Slot.ItemId || SlotTimes[i] <= 0.0)
		{
			// Une plante vient d'arriver : le séchage commence (ou reprend, après un chargement).
			const bool bResume = TimedItems[i].IsNone() && SlotTimes[i] > 0.0;
			TimedItems[i] = Slot.ItemId;
			if (!bResume)
			{
				SlotTimes[i] = Now;
			}
			bChanged = true;
			continue;
		}
		const FName Dried(*(TEXT("dried_") + Slot.ItemId.ToString()));
		if (Now - SlotTimes[i] >= ErewhonPlaceables::DryingSeconds && Items->Find(Dried))
		{
			Slot.ItemId = Dried;
			SlotTimes[i] = 0.0;
			TimedItems[i] = NAME_None;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		ForceNetUpdate();
		RefreshDisplay();
		OnStorageChanged.Broadcast();
	}
}

void AStorageActor::UpdatePotpourri(float DeltaSeconds)
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State || Carrier)
	{
		return;
	}
	const double Now = State->GetWorldClock();
	if (HasAuthority())
	{
		if (ExpiresAt > 0.0 && Now >= ExpiresAt)
		{
			Destroy();   // il a tout donné
			return;
		}
		// Chaque seconde : ceux qui passent tout près en repartent apaisés, pour un bon moment.
		CalmTimer += DeltaSeconds;
		if (CalmTimer >= 1.f)
		{
			CalmTimer = 0.f;
			for (TActorIterator<AErewhonCharacter> It(GetWorld()); It; ++It)
			{
				if (FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= ErewhonPlaceables::PotpourriRadius)
				{
					It->GrantEffect(EPotionEffect::Calm, ErewhonPlaceables::CalmSeconds);
				}
			}
		}
	}
	// Des pétales de lumière douce montent lentement en spirale, se fanent et renaissent.
	PetalTime += DeltaSeconds;
	for (int32 i = 0; i < Petals.Num(); ++i)
	{
		const float Life = FMath::Fmod(PetalTime * 0.18f + i / static_cast<float>(Petals.Num()), 1.f);
		const float Angle = PetalTime * 0.6f + i * 2.4f;
		const float Radius = 25.f + 90.f * Life;
		Petals[i]->SetRelativeLocation(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 15.f + Life * 160.f));
		Petals[i]->SetRelativeScale3D(FVector(FMath::Sin(Life * UE_PI) * 0.9f + 0.05f));
	}
}

void AStorageActor::OnRep_State()
{
	ApplyCollision();
}

AStorageActor* AStorageActor::TopOfStack()
{
	AStorageActor* Top = this;
	while (Top->Above)
	{
		Top = Top->Above;
	}
	return Top;
}

int32 AStorageActor::StackSize() const
{
	const AStorageActor* Bottom = this;
	while (Bottom->Below)
	{
		Bottom = Bottom->Below;
	}
	int32 Size = 1;
	for (const AStorageActor* It = Bottom; It->Above; It = It->Above)
	{
		++Size;
	}
	return Size;
}

bool AStorageActor::Accepts(const FItemDef& Def) const
{
	switch (Kind)
	{
	case EStorageKind::Crate:
		// Des plantes, et ce qu'on en tire de petit : teintures, pots-pourris.
		return Def.Category == EItemCategory::Plant || Def.Id.ToString().StartsWith(TEXT("dried_")) || Def.Id.ToString().StartsWith(TEXT("dye_"))
			|| Def.Id == TEXT("potpourri");
	case EStorageKind::Barrel: return Def.Category == EItemCategory::Plant && Water <= 0.01f;   // un tonneau de liquide ne tient que ça
	case EStorageKind::Shelf:  return ErewhonApothecary::IsShelvable(Def);
	case EStorageKind::DryingRack: return Def.Category == EItemCategory::Plant;
	case EStorageKind::FirewoodRack: return Def.Id == TEXT("firewood");
	case EStorageKind::LogPile: return Def.Id == ErewhonForestry::Log;
	case EStorageKind::Saddlebag: return Def.Category != EItemCategory::Placeable && Def.Category != EItemCategory::Backpack;
	case EStorageKind::Cauldron: return ErewhonApothecary::IsIngredient(Def) && (Water <= 0.01f || Liquid == ErewhonApothecary::Water || Liquid == ErewhonApothecary::BaseLiquid);
	default:                   return false;
	}
}

int32 AStorageActor::CountStored(FName ItemId) const
{
	int32 Total = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		Total += Slot.ItemId == ItemId ? Slot.Count : 0;
	}
	return Total;
}

int32 AStorageActor::StoreItem(FName ItemId, int32 Count)
{
	const UItemDatabase* Items = UItemDatabase::Get(this);
	const FItemDef* Def = Items ? Items->Find(ItemId) : nullptr;
	if (!Def || !Accepts(*Def) || Count <= 0)
	{
		return 0;
	}
	int32 Remaining = Count;
	for (const bool bEmptyPass : { false, true })
	{
		for (FInventorySlot& Slot : Slots)
		{
			if (Remaining == 0 || Slot.IsEmpty() != bEmptyPass || (!bEmptyPass && Slot.ItemId != ItemId))
			{
				continue;
			}
			const int32 Moved = FMath::Min(SlotCap(*Def) - (bEmptyPass ? 0 : Slot.Count), Remaining);
			if (Moved > 0)
			{
				Slot.ItemId = ItemId;
				Slot.Count = (bEmptyPass ? 0 : Slot.Count) + Moved;
				Remaining -= Moved;
			}
		}
	}
	if (Remaining != Count)
	{
		Commit();
	}
	return Count - Remaining;
}

void AStorageActor::Release()
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (Below)
	{
		Below->Above = nullptr;
		Below = nullptr;
	}
	if (Cart)
	{
		Cart->SetLoad(CartSlot, nullptr);
		Cart = nullptr;
		CartSlot = INDEX_NONE;
	}
	Carrier = nullptr;
	ApplyCollision();
}

void AStorageActor::PlaceOnGround(const FVector& Location, float Yaw)
{
	Release();
	SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f));
}

bool AStorageActor::StackOn(AStorageActor* Base)
{
	// Une cagette sur une cagette, trois au plus ; un mortier sur une table.
	const bool bCrates = Kind == EStorageKind::Crate && Base && Base->GetKind() == EStorageKind::Crate;
	const bool bOnTable = Kind == EStorageKind::Mortar && Base && Base->GetKind() == EStorageKind::Table && !Base->GetAbove();
	if (!Base || Base == this || (!bCrates && !bOnTable))
	{
		return false;
	}
	Base = Base->TopOfStack();
	if ((bCrates && Base->StackSize() >= ErewhonPlaceables::MaxStack) || Base == this)
	{
		return false;
	}
	Release();
	AttachToActor(Base, FAttachmentTransformRules::KeepRelativeTransform);
	SetActorRelativeLocation(FVector(0.f, 0.f, ErewhonPlaceables::Height(Base->GetKind())));
	SetActorRelativeRotation(FRotator(0.f, bOnTable ? 0.f : FMath::RandRange(-4.f, 4.f), 0.f));
	Below = Base;
	Base->Above = this;
	return true;
}

bool AStorageActor::LoadInto(ACart* InCart, int32 Slot)
{
	if (!InCart || Slot < 0 || Slot >= ACart::SlotCount || InCart->GetLoad(Slot) || InCart->HasWood())
	{
		return false;
	}
	Release();
	AttachToActor(InCart, FAttachmentTransformRules::KeepRelativeTransform);
	SetActorRelativeTransform(ACart::SlotTransform(Slot));
	Cart = InCart;
	CartSlot = Slot;
	InCart->SetLoad(Slot, this);
	return true;
}

void AStorageActor::CarryBy(APawn* Pawn)
{
	Release();
	// Devant soi, à hauteur des hanches ; le tonneau, plus grand, descend un peu.
	AttachToActor(Pawn, FAttachmentTransformRules::KeepRelativeTransform);
	FVector Offset(55.f, 0.f, -30.f);
	switch (Kind)
	{
	case EStorageKind::Barrel:   Offset = FVector(62.f, 0.f, -62.f); break;
	case EStorageKind::Shelf:    Offset = FVector(75.f, 0.f, -86.f); break;   // on la pousse devant soi
	case EStorageKind::Table:    Offset = FVector(85.f, 0.f, -86.f); break;
	case EStorageKind::Cauldron: Offset = FVector(55.f, 0.f, -55.f); break;
	case EStorageKind::Mortar:   Offset = FVector(45.f, 0.f, -10.f); break;
	default: break;
	}
	SetActorRelativeLocation(Offset);
	SetActorRelativeRotation(FRotator(0.f, 90.f, 0.f));
	Carrier = Pawn;
	ApplyCollision();
}

FText AStorageActor::GetInteractionLabel() const
{
	if (IsOwnedBy(LocalPlayerKey(this)))
	{
		switch (Kind)
		{
		case EStorageKind::Table:
			return LOCTEXT("MineTable", "Prendre : Table");
		case EStorageKind::Saddlebag:
			return LOCTEXT("MineSaddlebag", "Sacoche du bhurak    [E] / [F] Ouvrir");
		case EStorageKind::Mortar:
			return LOCTEXT("MineMortar", "Prendre : Mortier    [F] Piler des plantes");
		case EStorageKind::Cauldron:
			return ErewhonApothecary::HouseOfHungCauldron(this)
				? FText::Format(LOCTEXT("MineCauldronHung", "Prendre : Chaudron ({0} °C)    [F] S'en servir"), FText::AsNumber(FMath::RoundToInt32(Temperature)))
				: LOCTEXT("MineCauldron", "Prendre : Chaudron (à poser sur un support, dans la cheminée)    [F] S'en servir");
		case EStorageKind::CauldronStand:
			return FText::Format(LOCTEXT("MineStand", "Prendre : Support de chaudron (cran {0} / 10)    [F] Régler"), FText::AsNumber(FMath::RoundToInt32(Level * 10.f)));
		case EStorageKind::Shelf:
			return LOCTEXT("MineShelf", "Prendre : Étagère    [F] Ranger potions et écrasés");
		case EStorageKind::DryingRack:
			return LOCTEXT("MineRack", "Prendre : Séchoir    [F] Y mettre des plantes à sécher");
		case EStorageKind::FirewoodRack:
			return FText::Format(LOCTEXT("MineWoodRack", "Prendre : Rack à petit bois ({0})    [F] Ouvrir"), FText::AsNumber(CountStored(TEXT("firewood"))));
		case EStorageKind::LogPile:
			return FText::Format(LOCTEXT("MineLogPile", "Tas de bûches ({0} / {1}) : en reprendre deux    (des bûches en main : [Clic] pour les y poser)"),
				FText::AsNumber(CountStored(ErewhonForestry::Log)), FText::AsNumber(ErewhonForestry::PileCapacity));
		case EStorageKind::Workbench:
			return LOCTEXT("MineBench", "Prendre : Atelier    [F] Pots-pourris, teintures, bouquets");
		case EStorageKind::Potpourri:
		{
			const AErewhonGameState* State = AErewhonGameState::Get(this);
			const double Left = State ? FMath::Max(0.0, ExpiresAt - State->GetWorldClock()) : 0.0;
			return FText::Format(LOCTEXT("MinePotpourri", "Pot-pourri (il parfume encore {0} min)    [E] Reprendre"), FText::AsNumber(FMath::CeilToInt32(Left / 60.0)));
		}
		case EStorageKind::Barrel:
			if (Water > 0.01f)
			{
				return FText::Format(LOCTEXT("MineWater", "Prendre : Tonneau — {1} ({0} L)    [F] Ouvrir"), FText::AsNumber(FMath::FloorToInt32(Water)),
					ErewhonApothecary::LiquidName(Liquid).ToLower());
			}
			return FText::Format(LOCTEXT("Mine", "Prendre : {0}    [F] Ouvrir"), ErewhonPlaceables::KindName(Kind));
		default:
			return FText::Format(LOCTEXT("Mine", "Prendre : {0}    [F] Ouvrir"), ErewhonPlaceables::KindName(Kind));
		}
	}
	if (Kind == EStorageKind::FirewoodRack)
	{
		return FText::Format(LOCTEXT("TheirsWoodRack", "Rack à petit bois de {0} ({1})    [F] S'y servir"), FText::FromString(OwnerName), FText::AsNumber(CountStored(TEXT("firewood"))));
	}
	return FText::Format(LOCTEXT("Theirs", "{0} de {1}"), ErewhonPlaceables::KindName(Kind), FText::FromString(OwnerName));
}

void AStorageActor::Interact(AErewhonPlayerController* Controller)
{
	if (Controller && Kind == EStorageKind::LogPile)
	{
		Controller->ServerTakeLogs(this);   // un tas ne se déplace pas : on en reprend des bûches
		return;
	}
	if (Controller && Kind == EStorageKind::Saddlebag)
	{
		// La sacoche reste sur le harnais : on l'ouvre.
		if (IsOwnedBy(LocalPlayerKey(this)))
		{
			Controller->OpenStorage(this);
		}
		return;
	}
	if (Controller)
	{
		Controller->ServerPickUpStorage(this);
	}
}

// ── Charrette ────────────────────────────────────────────────────────────────

ACart::ACart()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Loads.SetNum(SlotCount);
	Wood.SetNum(WoodSlots);
}

void ACart::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACart, OwnerKey);
	DOREPLIFETIME(ACart, OwnerName);
	DOREPLIFETIME(ACart, Driver);
	DOREPLIFETIME(ACart, Loads);
	DOREPLIFETIME(ACart, Wood);
	DOREPLIFETIME(ACart, CartKind);
	DOREPLIFETIME(ACart, bEngaged);
}

void ACart::Setup(const FString& InOwnerKey, const FString& InOwnerName, ECartKind InKind)
{
	OwnerKey = InOwnerKey;
	OwnerName = InOwnerName;
	CartKind = InKind;
}

bool ACart::AcceptsBulk(FName ItemId) const
{
	switch (CartKind)
	{
	case ECartKind::Seeder: return ErewhonFarm::CropOfSeed(ItemId) != INDEX_NONE;
	case ECartKind::Cultivator: return false;
	case ECartKind::Harvester: return false;   // son coffre se remplit tout seul
	default: return ItemId == TEXT("firewood") || ItemId == ErewhonForestry::Log;
	}
}

float ACart::DragFactor() const
{
	if (!bEngaged)
	{
		return 1.f;
	}
	return IsImplement() ? 0.7f : 1.f;   // un outil au travail : 30 % moins vite
}

void ACart::SetEngaged(bool bOn)
{
	bEngaged = bOn && IsImplement();
	OnRep_Engaged();
	ForceNetUpdate();
}

void ACart::OnRep_Engaged()
{
	// Le cultivateur s'abaisse dans la terre ; le semoir ne bouge pas (sa roue tournera en roulant).
	if (ImplementBody && CartKind == ECartKind::Cultivator)
	{
		FTransform Lowered = ImplementRest;
		Lowered.AddToTranslation(FVector(0.f, 0.f, bEngaged ? -9.f : 0.f));
		ImplementBody->SetRelativeTransform(Lowered);
	}
}

void ACart::Work()
{
	using namespace ErewhonFarm;
	AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State || !bEngaged || !Cast<ABhurak>(Driver))
	{
		return;
	}
	const double Now = State->GetWorldClock();
	// Les cases sous l'outil : le cultivateur et la moissonneuse sur leur largeur (trois cases), le semoir trois sur trois autour de lui.
	TArray<FVector2D> Points;
	if (CartKind == ECartKind::Harvester)
	{
		for (const float Y : { -150.f, 0.f, 150.f })
		{
			Points.Add(FVector2D(GetActorTransform().TransformPosition(FVector(AxleX - 60.f, Y, 0.f))));
		}
	}
	else if (CartKind == ECartKind::Cultivator)
	{
		for (const float Y : { -150.f, 0.f, 150.f })
		{
			Points.Add(FVector2D(GetActorTransform().TransformPosition(FVector(AxleX, Y, 0.f))));
		}
	}
	else
	{
		for (const float X : { -200.f, 0.f, 200.f })
		{
			for (const float Y : { -200.f, 0.f, 200.f })
			{
				Points.Add(FVector2D(GetActorTransform().TransformPosition(FVector(AxleX + X, Y, 0.f))));
			}
		}
	}
	bool bWoodChanged = false;
	for (const FVector2D& Point : Points)
	{
		const FIntPoint Sector = ErewhonSector::FromWorld(FVector(Point, 0.f));
		const FString* FieldOwner = State->FindSectorOwnerKey(Sector);
		const int32 Cell = CellAt(Sector, Point);
		if (!FieldOwner || *FieldOwner != OwnerKey || !IsFieldCell(Sector, State->GetBuildings(Sector), Cell))
		{
			continue;
		}
		const FFieldCellEntry* Entry = State->FindFieldCell(Sector, Cell);
		if (CartKind == ECartKind::Harvester)
		{
			// Une culture mûre : coupée, sa récolte tombe dans le coffre (s'il a la place), la case redevient terre grossière.
			if (!Entry || Entry->Crop == 0 || Stage(Entry->SownAt, Now) < RipeStage || !Crops().IsValidIndex(Entry->Crop - 1))
			{
				continue;
			}
			const FCrop& Crop = Crops()[Entry->Crop - 1];
			int32 Room = 0;
			for (const FInventorySlot& Slot : Wood)
			{
				Room += Slot.IsEmpty() ? HopperStack : Slot.ItemId == Crop.Product ? FMath::Max(0, HopperStack - Slot.Count) : 0;
			}
			if (Room < Crop.Yield)
			{
				break;   // le coffre est plein
			}
			int32 Left = Crop.Yield;
			for (const bool bEmptyPass : { false, true })
			{
				for (FInventorySlot& Slot : Wood)
				{
					if (Left <= 0 || Slot.IsEmpty() != bEmptyPass || (!bEmptyPass && Slot.ItemId != Crop.Product))
					{
						continue;
					}
					const int32 Put = FMath::Min(Left, HopperStack - (bEmptyPass ? 0 : Slot.Count));
					Slot.ItemId = Crop.Product;
					Slot.Count = (bEmptyPass ? 0 : Slot.Count) + Put;
					Left -= Put;
				}
			}
			State->ClearFieldCell(Sector, Cell);
			bWoodChanged = true;
			continue;
		}
		if (CartKind == ECartKind::Cultivator)
		{
			if (!Entry)
			{
				State->SetFieldCell(Sector, Cell, 0, Now);
			}
			continue;
		}
		if (!Entry || Entry->Crop != 0)
		{
			continue;
		}
		// Un sac de graines sème neuf cases ; on en prend un dans la trémie quand le précédent est vide.
		if (SeedCredit <= 0)
		{
			FInventorySlot* Bag = Wood.FindByPredicate([](const FInventorySlot& S) { return !S.IsEmpty() && CropOfSeed(S.ItemId) != INDEX_NONE; });
			if (!Bag)
			{
				break;
			}
			SeedCrop = CropOfSeed(Bag->ItemId);
			SeedCredit = 9;
			if (--Bag->Count <= 0)
			{
				Bag->Clear();
			}
			bWoodChanged = true;
		}
		State->SetFieldCell(Sector, Cell, static_cast<uint8>(SeedCrop + 1), Now);
		--SeedCredit;
	}
	if (bWoodChanged)
	{
		CommitWood();
	}
}

FTransform ACart::SlotTransform(int32 Slot)
{
	// Deux places sur la plate-forme, l'une devant l'autre (repère : brancards vers −X).
	return FTransform(FRotator(0.f, 90.f, 0.f), FVector(Slot == 0 ? 62.f : 0.f, 0.f, 59.f));
}

void ACart::BeginPlay()
{
	Super::BeginPlay();
	FTransform Fit;
	if (IsImplement())
	{
		if (UStaticMesh* Tool = ErewhonPlaceables::ImplementMesh(CartKind, Fit))
		{
			ImplementBody = NewObject<UStaticMeshComponent>(this);
			ImplementBody->SetStaticMesh(Tool);
			ImplementBody->SetupAttachment(Root);
			ImplementBody->SetRelativeTransform(Fit);
			ImplementRest = Fit;
			ImplementBody->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			ImplementBody->RegisterComponent();
			AddInstanceComponent(ImplementBody);
		}
		else
		{
			// Repli : un châssis bas, deux brancards.
			ImplementBody = AddShape(this, Root, EShape::Cube, FVector(20, 0, 40), FVector(120, 150, 30), Palette::WoodDark, FRotator::ZeroRotator, true);
			ImplementRest = ImplementBody->GetRelativeTransform();
			for (const float Y : { -46.f, 46.f })
			{
				AddShape(this, Root, EShape::Cube, FVector(-90, Y, 60), FVector(130, 6, 6), Palette::WoodLight, FRotator::ZeroRotator, false);
			}
		}
		if (CartKind == ECartKind::Seeder)
		{
			// La petite roue de distribution, sur le flanc : elle tourne quand le semoir roule, enclenché.
			Spinner = NewObject<USceneComponent>(this);
			Spinner->SetupAttachment(Root);
			Spinner->SetRelativeLocation(FVector(AxleX + 20.f, 70.f, 55.f));
			Spinner->RegisterComponent();
			AddInstanceComponent(Spinner);
			for (int32 Spoke = 0; Spoke < 3; ++Spoke)
			{
				AddShape(this, Spinner, EShape::Cube, FVector::ZeroVector, FVector(48.f, 3.f, 5.f), Palette::WoodDark, FRotator(Spoke * 60.f, 0.f, 0.f), false);
			}
		}
		OnRep_Engaged();
		CommitWood();
		return;
	}
	if (UStaticMesh* Mesh = ErewhonPlaceables::CartMesh(Fit))
	{
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(this);
		Body->SetStaticMesh(Mesh);
		Body->SetupAttachment(Root);
		Body->SetRelativeTransform(Fit);
		Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Body->RegisterComponent();
		AddInstanceComponent(Body);

		// Les deux roues, à part, sur l'essieu : chacune tourne autour de l'axe Y de son pivot.
		UStaticMesh* WheelMesh = Mesh->GetName() == TEXT("SM_cart_body") ? LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_cart_wheel")) : nullptr;
		if (WheelMesh)
		{
			// Le disque de la roue doit se trouver dans le plan XZ : on le tourne s'il est dans le plan YZ.
			const FVector Size = WheelMesh->GetBoundingBox().GetSize();
			const float DiskYaw = Size.X < Size.Y ? 90.f : 0.f;
			const FTransform WheelFit = FitToHeight(WheelMesh, WheelDiameter) * FTransform(FRotator(0.f, DiskYaw, 0.f), FVector(0.f, 0.f, -WheelDiameter * 0.5f));
			for (const float Side : { -1.f, 1.f })
			{
				USceneComponent* Pivot = NewObject<USceneComponent>(this);
				Pivot->SetupAttachment(Root);
				Pivot->SetRelativeLocation(FVector(AxleX, Side * WheelHalfTrack, AxleZ));
				Pivot->RegisterComponent();
				AddInstanceComponent(Pivot);
				UStaticMeshComponent* Wheel = NewObject<UStaticMeshComponent>(this);
				Wheel->SetStaticMesh(WheelMesh);
				Wheel->SetupAttachment(Pivot);
				Wheel->SetRelativeTransform(WheelFit);
				Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Wheel->RegisterComponent();
				AddInstanceComponent(Wheel);
				WheelPivots.Add(Pivot);
				LastWheelPositions.Add(Pivot->GetComponentLocation());
				WheelAngles.Add(0.f);
			}
		}
		return;
	}
	// Repli : une plate-forme de planches, deux roues, deux brancards.
	const auto Plank = [this](const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator, EShape Shape = EShape::Cube)
	{
		UStaticMeshComponent* Part = AddShape(this, Root, Shape, Center, Size, Palette::WoodLight, Rotation, true);
		Part->SetMaterial(0, MakePaintedMaterial(Part, TEXT("wood_planks"), 90.f, Palette::WoodLight));
	};
	Plank(FVector(30, 0, 58), FVector(140, 92, 8));
	for (const float Y : { -46.f, 46.f })
	{
		Plank(FVector(30, Y, 72), FVector(140, 4, 24));
		Plank(FVector(30, Y * 1.18f, 40), FVector(80, 80, 6), FRotator(0.f, 0.f, 90.f), EShape::Cylinder);
		Plank(FVector(-90, Y * 0.75f, 64), FVector(130, 6, 6));
	}
}

FText ACart::GetInteractionLabel() const
{
	if (IsOwnedBy(LocalPlayerKey(this)) && CartKind == ECartKind::Cultivator)
	{
		return bEngaged ? LOCTEXT("CultivatorDown", "Cultivateur (baissé : il retourne la terre)    [E] Le lever    [F] Le tirer (devant les brancards)")
			: LOCTEXT("CultivatorUp", "Cultivateur (levé)    [E] Le baisser    [F] Le tirer (devant les brancards)");
	}
	if (IsOwnedBy(LocalPlayerKey(this)) && CartKind == ECartKind::Harvester)
	{
		return bEngaged ? LOCTEXT("HarvesterOn", "Moissonneuse (rabatteur baissé : elle coupe)    [E] Le lever    [F] Le coffre (sur le côté) ou la tirer (devant)")
			: LOCTEXT("HarvesterOff", "Moissonneuse (rabatteur levé)    [E] Le baisser    [F] Le coffre (sur le côté) ou la tirer (devant)");
	}
	if (IsOwnedBy(LocalPlayerKey(this)) && CartKind == ECartKind::Seeder)
	{
		return bEngaged ? LOCTEXT("SeederOn", "Semoir (roue enclenchée)    [E] Désenclencher    [F] La trémie (sur le côté) ou le tirer (devant)")
			: LOCTEXT("SeederOff", "Semoir (roue libre)    [E] Enclencher    [F] La trémie (sur le côté) ou le tirer (devant)");
	}
	if (IsOwnedBy(LocalPlayerKey(this)) && Cast<ABhurak>(Driver))
	{
		return LOCTEXT("CartTowed", "Charrette attelée au bhurak    [F] Ouvrir (sur le côté)    [G] sur le bhurak : dételer");
	}
	if (IsOwnedBy(LocalPlayerKey(this)))
	{
		return LOCTEXT("CartMine", "Charrette    [F] La conduire (devant les brancards), ou y charger du petit bois (sur le côté)");
	}
	return FText::Format(LOCTEXT("CartTheirs", "Charrette de {0}"), FText::FromString(OwnerName));
}

void ACart::Interact(AErewhonPlayerController* Controller)
{
	if (Controller && IsImplement() && IsOwnedBy(LocalPlayerKey(this)))
	{
		Controller->ServerToggleImplement(this);   // le levier
		return;
	}
	if (Controller)
	{
		Controller->ShowToast(LOCTEXT("CartHint", "Placez-vous devant les brancards et appuyez sur F pour la conduire."));
	}
}

void ACart::CommitWood()
{
	// Quelques bûches couchées sur la plate-forme, plus il y en a, plus il y en a.
	if (GetNetMode() != NM_DedicatedServer)
	{
		for (UStaticMeshComponent* Part : WoodParts)
		{
			if (Part)
			{
				Part->DestroyComponent();
			}
		}
		WoodParts.Reset();
		int32 Count = 0;
		for (const FInventorySlot& Slot : Wood)
		{
			Count += Slot.IsEmpty() ? 0 : Slot.Count;
		}
		// Les grosses bûches, couchées en long sur la plate-forme ; le petit bois, en piles.
		int32 BigLogs = 0;
		for (const FInventorySlot& Slot : Wood)
		{
			BigLogs += Slot.ItemId == ErewhonForestry::Log ? Slot.Count : 0;
		}
		Count -= BigLogs;
		UStaticMesh* LogMesh = IsImplement() ? nullptr : LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_log"));
		for (int32 i = 0; i < BigLogs && LogMesh; ++i)
		{
			const FVector At(10.f, -45.f + 30.f * (i % 4), 60.f + 26.f * (i / 4));
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
			Part->SetStaticMesh(LogMesh);
			Part->SetupAttachment(Root);
			Part->SetRelativeTransform(FitToLength(LogMesh, 100.f, 28.f) * FTransform(FRotator(0.f, 90.f, 0.f), At));
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->RegisterComponent();
			AddInstanceComponent(Part);
			WoodParts.Add(Part);
		}
		UStaticMesh* Logs = IsImplement() ? nullptr : LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_firewood"));
		const int32 Piles = FMath::Min(6, (Count + 4) / 5);
		for (int32 i = 0; i < Piles && Logs; ++i)
		{
			const FVector At(-20.f + 40.f * (i % 3), (i / 3 % 2 ? 22.f : -22.f), 62.f + 10.f * (i / 6));
			WoodParts.Add(AddGeneratedMesh(this, Root, Logs, At, 22.f, 90.f + (i % 2) * 15.f, false));
		}
	}
	ForceNetUpdate();
	OnWoodChanged.Broadcast();
}

int32 ACart::FreeSlotNear(const FVector& World) const
{
	if (HasWood() || IsImplement())
	{
		return INDEX_NONE;   // le petit bois occupe toute la plate-forme
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(World);
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const float Distance = FVector::Dist2D(Local, SlotTransform(Slot).GetLocation());
		if (!GetLoad(Slot) && Distance < BestDistance)
		{
			Best = Slot;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool ACart::IsAtHandles(const FVector& World) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(World);
	return Local.X < -70.f && Local.X > -320.f && FMath::Abs(Local.Y) < 140.f;
}

void ACart::SetLoad(int32 Slot, AStorageActor* Storage)
{
	if (Loads.IsValidIndex(Slot))
	{
		Loads[Slot] = Storage;
	}
}

void ACart::ApplyDriverCollision(APawn* Pawn, bool bIgnore)
{
	// Le conducteur ne doit pas buter contre sa charrette ni son chargement.
	if (!Pawn)
	{
		return;
	}
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, true, true);
	Attached.Add(this);
	for (AActor* Actor : Attached)
	{
		if (bIgnore)
		{
			Pawn->MoveIgnoreActorAdd(Actor);
		}
		else
		{
			Pawn->MoveIgnoreActorRemove(Actor);
		}
	}
}

void ACart::OnRep_Driver()
{
	if (AppliedDriver.IsValid() && AppliedDriver.Get() != Driver)
	{
		ApplyDriverCollision(AppliedDriver.Get(), false);
	}
	AppliedDriver = Driver;
	ApplyDriverCollision(Driver, true);
}

void ACart::StartDriving(APawn* Pawn)
{
	// Le conducteur prend les brancards : la charrette le suivra (voir FollowDriver).
	Driver = Pawn;
	OnRep_Driver();
}

void ACart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Driver)
	{
		FollowDriver();
	}
	SpinWheels();
	if (IsImplement() && HasAuthority())
	{
		WorkTimer += DeltaSeconds;
		if (WorkTimer >= 0.2f)
		{
			WorkTimer = 0.f;
			Work();
		}
	}
	if (Spinner && bEngaged && DeltaSeconds > 0.f)
	{
		SpinAngle = FMath::Fmod(SpinAngle + RollSpeed * DeltaSeconds * 1.2f, 360.f);
		Spinner->SetRelativeRotation(FRotator(SpinAngle, 0.f, 0.f));
	}

	// Le bruit des roues : il suit la vitesse (adoucie), et se tait à l'arrêt.
	if (GetNetMode() != NM_DedicatedServer && DeltaSeconds > 0.f)
	{
		const float Speed = FVector::Dist2D(GetActorLocation(), LastRollLocation) / DeltaSeconds;
		LastRollLocation = GetActorLocation();
		RollSpeed = FMath::FInterpTo(RollSpeed, Speed < 1000.f ? Speed : 0.f, DeltaSeconds, 6.f);
		if (!RollAudio)
		{
			if (USoundWave* Roll = ErewhonSound::Load(TEXT("amb_cart_roll")))
			{
				RollAudio = NewObject<UAudioComponent>(this);
				RollAudio->SetupAttachment(Root);
				RollAudio->SetRelativeLocation(FVector(AxleX, 0.f, AxleZ));
				RollAudio->SetSound(Roll);
				RollAudio->AttenuationSettings = ErewhonSound::Attenuation(200.f, 1800.f);
				RollAudio->bAutoActivate = false;
				RollAudio->RegisterComponent();
			}
		}
		if (RollAudio)
		{
			const float Level = FMath::Clamp((RollSpeed - 15.f) / 160.f, 0.f, 1.f);
			if (Level > 0.01f && !RollAudio->IsPlaying())
			{
				RollAudio->Play(FMath::FRandRange(0.f, 6.f));
			}
			else if (Level <= 0.01f && RollAudio->IsPlaying())
			{
				RollAudio->Stop();
			}
			RollAudio->SetVolumeMultiplier(0.9f * Level * ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Effects));
			RollAudio->SetPitchMultiplier(0.85f + 0.3f * Level);
		}
	}
}

void ACart::SpinWheels()
{
	// Chaque roue roule sans glisser : elle tourne du chemin parcouru dans l'axe de la charrette.
	const FVector Forward = GetActorForwardVector();
	for (int32 i = 0; i < WheelPivots.Num(); ++i)
	{
		USceneComponent* Pivot = WheelPivots[i].Get();
		if (!Pivot)
		{
			continue;
		}
		const FVector Position = Pivot->GetComponentLocation();
		const float Travel = FVector::DotProduct(Position - LastWheelPositions[i], Forward);
		LastWheelPositions[i] = Position;
		if (FMath::Abs(Travel) < 0.01f || FMath::Abs(Travel) > 500.f)
		{
			continue;   // immobile, ou téléportée
		}
		WheelAngles[i] = FMath::Fmod(WheelAngles[i] - FMath::RadiansToDegrees(Travel / (WheelDiameter * 0.5f)), 360.f);
		Pivot->SetRelativeRotation(FRotator(WheelAngles[i], 0.f, 0.f));
	}
}

void ACart::PostNetReceiveLocationAndRotation()
{
	// Conduite : chaque machine la fait rouler elle-même derrière le conducteur, sans à-coups réseau.
	if (!Driver)
	{
		Super::PostNetReceiveLocationAndRotation();
	}
}

float ACart::GroundAt(const FVector& World) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonCartGround), false, this);
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	if (Driver)
	{
		Params.AddIgnoredActor(Driver);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, World + FVector(0, 0, 150), World - FVector(0, 0, 400), ECC_WorldStatic, Params))
	{
		return Hit.ImpactPoint.Z;
	}
	return World.Z;
}

void ACart::FollowDriver()
{
	// Comme une charrette à bras qu'on tire : le bout des brancards suit le joueur, et l'essieu
	// suit le bout des brancards à distance constante. La charrette pivote donc autour de ses
	// roues ; si l'on recule vers elle, elle est poussée et roule à l'envers.
	const float Length = AxleX - GripX;
	const ABhurak* Beast = Cast<ABhurak>(Driver);
	const FVector2D Grip(Beast ? Beast->GetHitchPoint() : Driver->GetActorLocation());
	FVector2D Axle(GetActorTransform().TransformPosition(FVector(AxleX, 0.f, 0.f)));
	FVector2D Along = Axle - Grip;
	if (Along.SizeSquared() < 1.f)
	{
		return;
	}
	Along.Normalize();
	Axle = Grip + Along * Length;
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
	const FVector2D Origin = Axle - Along * AxleX;
	const float Ground = GroundAt(FVector(Axle, GetActorLocation().Z));
	const FTransform Wanted(FRotator(0.f, Yaw, 0.f), FVector(Origin, Ground));
	if (Cast<ACharacter>(Driver) && IsBlockedAt(Wanted))
	{
		// Bloquée : elle reste où elle est, et retient qui la tire (au bout des brancards).
		if (Driver->HasAuthority() || Driver->IsLocallyControlled())
		{
			const FVector Hold = GetActorTransform().TransformPosition(FVector(GripX, 0.f, 0.f));
			Driver->SetActorLocation(FVector(Hold.X, Hold.Y, Driver->GetActorLocation().Z));
		}
		return;
	}
	SetActorLocationAndRotation(Wanted.GetLocation(), Wanted.Rotator());
}

bool ACart::IsBlockedAt(const FTransform& Where) const
{
	// La caisse, au-dessus des roues (pour ne pas buter sur le sol en pente).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonCartBlock), false, this);
	TArray<AActor*> Ignored;
	GetAttachedActors(Ignored, true, true);
	Params.AddIgnoredActors(Ignored);
	if (Driver)
	{
		Params.AddIgnoredActor(Driver);
	}
	const FVector Center = Where.TransformPosition(FVector(25.f, 0.f, 72.f));
	return GetWorld()->OverlapBlockingTestByChannel(Center, Where.GetRotation(), ECC_Pawn, FCollisionShape::MakeBox(FVector(68.f, 48.f, 16.f)), Params);
}

void ACart::StopDriving()
{
	APawn* Previous = Driver;
	Driver = nullptr;
	OnRep_Driver();
	if (Previous)
	{
		ApplyDriverCollision(Previous, false);
	}
	// Reposée sur le sol, à plat.
	FVector Location = GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ErewhonCartDrop), false, this);
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	if (Previous)
	{
		Params.AddIgnoredActor(Previous);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0, 0, 150), Location - FVector(0, 0, 400), ECC_WorldStatic, Params))
	{
		Location = Hit.ImpactPoint;
	}
	SetActorLocationAndRotation(Location, FRotator(0.f, GetActorRotation().Yaw, 0.f));
}

#undef LOCTEXT_NAMESPACE
