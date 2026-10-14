#include "World/Apothecary.h"

#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ErewhonGameState.h"
#include "Core/ErewhonPlayerController.h"
#include "Core/ErewhonPlayerState.h"
#include "Core/ErewhonSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/ItemDatabase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "World/Door.h"
#include "World/ErewhonSound.h"
#include "World/ErewhonVisuals.h"
#include "World/SectorTypes.h"
#include "World/SpawnCabin.h"
#include "World/SpawnLayout.h"
#include "World/Storage.h"
#include "World/Terrain.h"

#define LOCTEXT_NAMESPACE "ErewhonApothecary"

using namespace ErewhonVisuals;

namespace
{
	/** Chaque potion et la plante dont l'écrasé lui donne son effet. */
	const TPair<const TCHAR*, const TCHAR*> GPotions[] = {
		{ TEXT("potion_speed"), TEXT("flower_blue") },
		{ TEXT("potion_night"), TEXT("flower_purple") },
		{ TEXT("potion_strength"), TEXT("weed_nettle") },
		{ TEXT("potion_jump"), TEXT("fern") },
		{ TEXT("potion_shrink"), TEXT("herb") },
		{ TEXT("potion_grow"), TEXT("weed_dock") },
	};

	FName Crushed(FName Plant) { return FName(*(TEXT("crushed_") + Plant.ToString())); }

	const FLinearColor GFireColor = FLinearColor::FromSRGBColor(FColor(255, 150, 60));
	const FLinearColor GBark = FLinearColor::FromSRGBColor(FColor(96, 70, 48));
}

// ── Plan, étagères et recettes ───────────────────────────────────────────────

namespace ErewhonApothecary
{
	bool HasFireplaceModel()
	{
		static const bool bModel = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_fireplace")) != nullptr;
		return bModel;
	}

	FVector HearthLocal()
	{
		// Dans le modèle, la sole du foyer est à 10 % de la hauteur, au milieu de la profondeur de l'ouverture.
		const float Back = ErewhonSpawn::CabinHalfLength - ErewhonSpawn::CabinWallThickness;
		return HasFireplaceModel()
			? FVector(Back - 62.f, 0.f, ErewhonSpawn::CabinFloorTop + 23.f)
			: FVector(Back - 42.f, 0.f, ErewhonSpawn::CabinFloorTop + 8.f);
	}
	FVector StandLocal() { return HearthLocal() * FVector(1.f, 1.f, 0.f) + FVector(0.f, 0.f, ErewhonSpawn::CabinFloorTop); }

	bool IsShelvable(const FItemDef& Def)
	{
		const FString Id = Def.Id.ToString();
		return Def.IsPotion() || Id == TEXT("vial") || Id == TEXT("potion_base") || Id == TEXT("potpourri") || Id.StartsWith(TEXT("crushed_")) || Id.StartsWith(TEXT("dye_"));
	}

	float ShelfRowZ(int32 Row)
	{
		// Le dessus des planches : celles du modèle généré (mesurées), ou celles de BuildShelfFrame.
		static const bool bModel = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_shelf")) != nullptr;
		const float ModelRows[ShelfRows] = { 10.5f, 51.5f, 95.5f, 140.f };
		return bModel ? ModelRows[FMath::Clamp(Row, 0, ShelfRows - 1)] : 8.f + 46.f * Row;
	}

	void BuildShelfFrame(AActor* Owner, USceneComponent* Parent)
	{
		if (UStaticMesh* Model = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_shelf")))
		{
			// Le modèle peint, façade vers −X comme les planches (sa collision : la boîte du meuble).
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
			Part->SetStaticMesh(Model);
			Part->SetupAttachment(Parent);
			Part->SetRelativeTransform(FitToBox(Model, ShelfWidth, ShelfDepth, ShelfHeight));
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->RegisterComponent();
			Owner->AddInstanceComponent(Part);
			return;
		}
		constexpr float Board = 4.f;
		UMaterialInterface* Wood = MakePaintedMaterial(Owner, TEXT("wood_planks"), 90.f, Palette::WoodLight, FLinearColor(0.85f, 0.75f, 0.62f));
		const auto Plank = [Owner, Parent, Wood](const FVector& Center, const FVector& Size)
		{
			AddShape(Owner, Parent, EShape::Cube, Center, Size, Palette::WoodLight, FRotator::ZeroRotator, false)->SetMaterial(0, Wood);
		};
		for (const float SideY : { -1.f, 1.f })
		{
			Plank(FVector(0.f, SideY * (ShelfWidth * 0.5f - Board * 0.5f), ShelfHeight * 0.5f), FVector(ShelfDepth, Board, ShelfHeight));
		}
		Plank(FVector(ShelfDepth * 0.5f - 1.f, 0.f, ShelfHeight * 0.5f), FVector(2.f, ShelfWidth, ShelfHeight));   // le fond
		for (const float Z : { 6.f, 52.f, 98.f, 144.f, ShelfHeight - Board * 0.5f })
		{
			Plank(FVector(0.f, 0.f, Z), FVector(ShelfDepth, ShelfWidth - 2 * Board, Board));
		}
	}

	void BuildShelfDisplay(AActor* Owner, USceneComponent* Parent, const TArray<FInventorySlot>& Slots, TArray<TObjectPtr<UPrimitiveComponent>>& Parts)
	{
		for (UPrimitiveComponent* Part : Parts)
		{
			if (Part)
			{
				Part->DestroyComponent();
			}
		}
		Parts.Reset();
		const UItemDatabase* Items = UItemDatabase::Get(Owner);
		if (!Items || Owner->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		const FLinearColor Cork = FLinearColor::FromSRGBColor(FColor(150, 110, 70));
		UStaticMesh* Bowl = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_bowl"));
		UMaterialInterface* GlowMaterial = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Glow"));
		UTexture* GlowTexture = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_star"));
		for (int32 Index = 0; Index < Slots.Num() && Index < ShelfSlots; ++Index)
		{
			const int32 Row = Index / ShelfSlotsPerRow;
			const int32 Column = Index % ShelfSlotsPerRow;
			const FInventorySlot& Slot = Slots[Index];
			const FItemDef* Def = Slot.IsEmpty() ? nullptr : Items->Find(Slot.ItemId);
			if (!Def)
			{
				continue;
			}
			// Un emplacement : un flacon (ou un bol) devant, un second derrière s'il y en a plusieurs.
			const bool bCrushed = Def->Id.ToString().StartsWith(TEXT("crushed_"));
			UStaticMesh* Model = bCrushed ? Bowl : Cast<UStaticMesh>(Def->Mesh.TryLoad());
			UMaterialInstanceDynamic* Glow = nullptr;
			if (Def->IsPotion() && GlowMaterial && GlowTexture)
			{
				Glow = UMaterialInstanceDynamic::Create(GlowMaterial, Owner);
				Glow->SetVectorParameterValue(TEXT("Color"), Def->Color);
				Glow->SetScalarParameterValue(TEXT("Seed"), Index * 1.7f);
			}
			for (int32 k = 0; k < FMath::Min(Slot.Count, 2); ++k)
			{
				const float SlotY = (Column - (ShelfSlotsPerRow - 1) * 0.5f) * (ShelfWidth - 20.f) / ShelfSlotsPerRow;
				const FVector Foot(k == 0 ? -8.f : 6.f, SlotY + (k == 0 ? 0.f : 5.f), ShelfRowZ(Row));
				if (Model)
				{
					Parts.Add(AddGeneratedMesh(Owner, Parent, Model, Foot, bCrushed ? 6.f : 20.f, 180.f + (k % 3 - 1) * 14.f, false));
					if (bCrushed)
					{
						// La pâte de plante dans le bol, à sa couleur.
						Parts.Add(AddShape(Owner, Parent, EShape::Cylinder, Foot + FVector(0, 0, 4.6f), FVector(8.5f, 8.5f, 1.4f), Def->Color, FRotator::ZeroRotator, false));
					}
				}
				else
				{
					Parts.Add(AddShape(Owner, Parent, EShape::Sphere, Foot + FVector(0, 0, 7.f), FVector(11.f, 11.f, 14.f), Def->Color, FRotator::ZeroRotator, false));
					Parts.Add(AddShape(Owner, Parent, EShape::Cylinder, Foot + FVector(0, 0, 16.f), FVector(4.f, 4.f, 5.f), Cork, FRotator::ZeroRotator, false));
				}
				if (Glow)
				{
					// Une petite lueur de la couleur de la potion, qui palpite doucement devant le flacon.
					if (UProceduralMeshComponent* Halo = AddPicture(Owner, Parent, Foot + FVector(-9.f, 0.f, 9.f), 26.f, 26.f, 0.f, GlowTexture))
					{
						Halo->SetMaterial(0, Glow);
						Parts.Add(Halo);
					}
				}
			}
		}
		for (UPrimitiveComponent* Part : Parts)
		{
			if (Part)
			{
				Part->SetCastShadow(false);
			}
		}
	}

	int32 DoorSide(const FIntPoint& Sector)
	{
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (ErewhonSector::IsRoad(Sector + ErewhonSector::Sides[Side]))
			{
				return Side;
			}
		}
		// Loin des routes : la porte regarde vers le village.
		if (FMath::Abs(Sector.X) >= FMath::Abs(Sector.Y))
		{
			return Sector.X > 0 ? 1 : 0;
		}
		return Sector.Y > 0 ? 3 : 2;
	}

	FTransform HouseTransform(const FIntPoint& Sector, int32 WorldSeed)
	{
		// La porte (−X de la maison) regarde vers DoorSide ; la maison repose sur le point haut de son emprise.
		const FVector2D Toward(ErewhonSector::Sides[DoorSide(Sector)]);
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Toward.Y, -Toward.X));
		const FVector2D Center(ErewhonSector::Center(Sector));
		const FRotator Rotation(0.f, Yaw, 0.f);
		float Top = ErewhonTerrain::Height(Center, WorldSeed);
		for (const float SX : { -1.f, 1.f })
		{
			for (const float SY : { -1.f, 1.f })
			{
				const FVector Corner = Rotation.RotateVector(FVector(SX * (ErewhonSpawn::CabinHalfLength + 55.f), SY * (ErewhonSpawn::CabinHalfWidth + 55.f), 0.f));
				Top = FMath::Max(Top, ErewhonTerrain::Height(Center + FVector2D(Corner), WorldSeed));
			}
		}
		return FTransform(Rotation, FVector(Center, Top));
	}

	bool HasHouse(const AErewhonGameState* State, const FIntPoint& Sector)
	{
		return State && (State->GetBuildings(Sector) & ESectorBuilding::Apothecary) != 0;
	}

	bool HasStoreroom(const AErewhonGameState* State, const FIntPoint& Sector)
	{
		return HasHouse(State, Sector) && (State->GetBuildings(Sector) & ESectorBuilding::Storeroom) != 0;
	}

	TOptional<FTransform> StandSpot(const UObject* WorldContext, const FIntPoint& Sector)
	{
		const AErewhonGameState* State = AErewhonGameState::Get(WorldContext);
		if (!HasHouse(State, Sector))
		{
			return {};
		}
		const FTransform House = HouseTransform(Sector, State->GetWorldSeed());
		return FTransform(House.Rotator(), House.TransformPosition(StandLocal()));
	}

	AStorageActor* FindStand(const UObject* WorldContext, const FIntPoint& Sector)
	{
		const TOptional<FTransform> Spot = StandSpot(WorldContext, Sector);
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!Spot.IsSet() || !World)
		{
			return nullptr;
		}
		for (TActorIterator<AStorageActor> It(World); It; ++It)
		{
			if (It->GetKind() == EStorageKind::CauldronStand && !It->GetCarrier() && FVector::Dist(It->GetActorLocation(), Spot->GetLocation()) < 30.f)
			{
				return *It;
			}
		}
		return nullptr;
	}

	FTransform CauldronSpotOn(const AStorageActor* Stand)
	{
		// Bas : le fond du chaudron touche presque les flammes ; haut : il pend 45 cm plus haut.
		return FTransform(Stand->GetActorRotation(), Stand->GetActorLocation() + FVector(0.f, 0.f, 30.f + 45.f * Stand->GetLevel()));
	}

	AApothecary* FindHouse(const UObject* WorldContext, const FIntPoint& Sector)
	{
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AApothecary> It(World); It; ++It)
		{
			if (It->GetSector() == Sector)
			{
				return *It;
			}
		}
		return nullptr;
	}

	AApothecary* HouseOfHungCauldron(const AStorageActor* Cauldron, AStorageActor** OutStand)
	{
		if (!Cauldron || Cauldron->GetKind() != EStorageKind::Cauldron || Cauldron->GetCarrier())
		{
			return nullptr;
		}
		const FIntPoint Sector = ErewhonSector::FromWorld(Cauldron->GetActorLocation());
		AStorageActor* Stand = FindStand(Cauldron, Sector);
		if (!Stand || FVector::Dist(CauldronSpotOn(Stand).GetLocation(), Cauldron->GetActorLocation()) > 20.f)
		{
			return nullptr;
		}
		if (OutStand)
		{
			*OutStand = Stand;
		}
		return FindHouse(Cauldron, Sector);
	}

	bool IsInside(const UObject* WorldContext, const FVector& World)
	{
		const AErewhonGameState* State = AErewhonGameState::Get(WorldContext);
		const FIntPoint Sector = ErewhonSector::FromWorld(World);
		if (!HasHouse(State, Sector))
		{
			return false;
		}
		const FVector Local = HouseTransform(Sector, State->GetWorldSeed()).InverseTransformPosition(World);
		if (Local.Z < ErewhonSpawn::CabinFloorTop - 20.f)
		{
			return false;
		}
		const bool bMain = FMath::Abs(Local.X) < ErewhonSpawn::CabinHalfLength && FMath::Abs(Local.Y) < ErewhonSpawn::CabinHalfWidth;
		const bool bStore = HasStoreroom(State, Sector) && FMath::Abs(Local.X) < StoreroomHalfLength
			&& Local.Y >= ErewhonSpawn::CabinHalfWidth && Local.Y < ErewhonSpawn::CabinHalfWidth + StoreroomDepth;
		return bMain || bStore;
	}

	const TArray<FBrew>& Brews()
	{
		static const TArray<FBrew> List = []()
		{
			const auto C = [](const TCHAR* Plant) { return FName(*(FString(TEXT("crushed_")) + Plant)); };
			const auto D = [](const TCHAR* Plant) { return FName(*(FString(TEXT("dried_")) + Plant)); };
			TArray<FBrew> Out;
			// La base : de l'herbe, beaucoup d'herbe, à feu doux.
			Out.Add({ TEXT("brew_base"), TEXT("potion_base"), NSLOCTEXT("ErewhonBrew", "Base", "Base de potion"),
				{ { C(TEXT("grass")), 20.f } }, 40.f, 65.f, 15.f });
			Out.Add({ TEXT("brew_speed"), TEXT("potion_speed"), NSLOCTEXT("ErewhonBrew", "Speed", "Breuvage de vitesse"),
				{ { C(TEXT("flower_blue")), 5.f }, { C(TEXT("grass")), 2.f } }, 60.f, 70.f, 20.f });
			Out.Add({ TEXT("brew_night"), TEXT("potion_night"), NSLOCTEXT("ErewhonBrew", "Night", "Breuvage de nyctalopie"),
				{ { C(TEXT("flower_purple")), 5.f }, { D(TEXT("herb")), 2.f } }, 83.f, 87.f, 25.f });
			Out.Add({ TEXT("brew_strength"), TEXT("potion_strength"), NSLOCTEXT("ErewhonBrew", "Strength", "Breuvage de force"),
				{ { C(TEXT("weed_nettle")), 6.f }, { C(TEXT("broadleaf")), 2.f } }, 92.f, 100.f, 20.f });
			Out.Add({ TEXT("brew_jump"), TEXT("potion_jump"), NSLOCTEXT("ErewhonBrew", "Jump", "Breuvage de saut"),
				{ { C(TEXT("fern")), 5.f }, { C(TEXT("flower_white")), 2.f } }, 70.f, 78.f, 20.f });
			Out.Add({ TEXT("brew_shrink"), TEXT("potion_shrink"), NSLOCTEXT("ErewhonBrew", "Shrink", "Breuvage de rapetissement"),
				{ { C(TEXT("herb")), 4.f }, { D(TEXT("flower_white")), 3.f } }, 52.f, 58.f, 30.f });
			Out.Add({ TEXT("brew_grow"), TEXT("potion_grow"), NSLOCTEXT("ErewhonBrew", "Grow", "Breuvage de grandissement"),
				{ { C(TEXT("weed_dock")), 5.f }, { TEXT("mushroom_cep"), 1.f } }, 100.f, 106.f, 25.f });
			// La lumière : des girolles dorées et des fleurs blanches ; on luit autour de soi.
			Out.Add({ TEXT("brew_light"), TEXT("potion_light"), NSLOCTEXT("ErewhonBrew", "Light", "Breuvage de lumière"),
				{ { TEXT("mushroom_chanterelle"), 2.f }, { C(TEXT("flower_white")), 3.f } }, 74.f, 80.f, 25.f });
			return Out;
		}();
		return List;
	}

	FName SolventOf(const FBrew& Brew)
	{
		return Brew.Liquid == BaseLiquid ? Water : BaseLiquid;
	}

	const FBrew* FindBrew(FName Liquid)
	{
		return Brews().FindByPredicate([Liquid](const FBrew& B) { return B.Liquid == Liquid; });
	}

	const FBrew* SameIngredientsBrew(const TArray<FInventorySlot>& Ingredients)
	{
		TSet<FName> Put;
		for (const FInventorySlot& Slot : Ingredients)
		{
			if (!Slot.IsEmpty())
			{
				Put.Add(Slot.ItemId);
			}
		}
		return Brews().FindByPredicate([&Put](const FBrew& Brew)
		{
			if (Brew.PerLiter.Num() != Put.Num())
			{
				return false;
			}
			for (const TPair<FName, float>& Need : Brew.PerLiter)
			{
				if (!Put.Contains(Need.Key))
				{
					return false;
				}
			}
			return true;
		});
	}

	const FBrew* MatchBrew(const TArray<FInventorySlot>& Ingredients, float Liters)
	{
		if (Liters < 0.5f)
		{
			return nullptr;
		}
		TMap<FName, int32> Totals;
		for (const FInventorySlot& Slot : Ingredients)
		{
			if (!Slot.IsEmpty())
			{
				Totals.FindOrAdd(Slot.ItemId) += Slot.Count;
			}
		}
		for (const FBrew& Brew : Brews())
		{
			bool bMatch = Totals.Num() == Brew.PerLiter.Num();
			for (const TPair<FName, float>& Need : Brew.PerLiter)
			{
				const int32* Have = Totals.Find(Need.Key);
				const float Wanted = Need.Value * Liters;
				bMatch &= Have && *Have >= Wanted * 0.8f - 0.5f && *Have <= Wanted * 1.2f + 0.5f;
			}
			if (bMatch)
			{
				return &Brew;
			}
		}
		return nullptr;
	}

	bool IsIngredient(const FItemDef& Def)
	{
		const FString Id = Def.Id.ToString();
		return Def.Category == EItemCategory::Plant || Id.StartsWith(TEXT("crushed_")) || Id.StartsWith(TEXT("dried_"));
	}

	FText LiquidName(FName Liquid)
	{
		if (Liquid == Water)
		{
			return NSLOCTEXT("ErewhonBrew", "Water", "Eau");
		}
		const FBrew* Brew = FindBrew(Liquid);
		return Brew ? Brew->Name : NSLOCTEXT("ErewhonBrew", "Nothing", "Vide");
	}

	FLinearColor LiquidColor(FName Liquid)
	{
		if (const FBrew* Brew = FindBrew(Liquid))
		{
			static const TMap<FName, FLinearColor> Colors = {
				{ TEXT("brew_base"), FLinearColor(0.62f, 0.75f, 0.45f) }, { TEXT("brew_light"), FLinearColor(1.f, 0.85f, 0.45f) },
				{ TEXT("brew_speed"), FLinearColor(0.25f, 0.5f, 1.f) }, { TEXT("brew_night"), FLinearColor(0.45f, 0.2f, 0.75f) },
				{ TEXT("brew_strength"), FLinearColor(0.15f, 0.55f, 0.2f) }, { TEXT("brew_jump"), FLinearColor(0.55f, 0.85f, 0.2f) },
				{ TEXT("brew_shrink"), FLinearColor(0.95f, 0.6f, 0.8f) }, { TEXT("brew_grow"), FLinearColor(0.95f, 0.55f, 0.15f) } };
			const FLinearColor* Found = Colors.Find(Brew->Liquid);
			return Found ? *Found : FLinearColor::White;
		}
		return FLinearColor(0.55f, 0.75f, 0.9f);
	}

	FName PitcherFor(FName Liquid, int32 Liters)
	{
		if (Liquid.IsNone() || Liters <= 0)
		{
			return EmptyPitcher;
		}
		return FName(*(TEXT("pitcher_") + Liquid.ToString() + (Liters == 1 ? TEXT("_1l") : TEXT(""))));
	}

	FName LiquidOfPitcher(FName Item)
	{
		FString Id = Item.ToString();
		if (!Id.StartsWith(TEXT("pitcher_")))
		{
			return NAME_None;
		}
		Id.RemoveFromEnd(TEXT("_1l"));
		return FName(*Id.RightChop(8));
	}

	int32 PitcherLitersOf(FName Item)
	{
		if (Item == EmptyPitcher)
		{
			return 0;
		}
		if (LiquidOfPitcher(Item).IsNone())
		{
			return -1;
		}
		return Item.ToString().EndsWith(TEXT("_1l")) ? 1 : 2;
	}

	TArray<AStorageActor*> WoodRacksNear(const AApothecary* House)
	{
		TArray<AStorageActor*> Out;
		const TOptional<FTransform> Spot = House ? StandSpot(House, House->GetSector()) : TOptional<FTransform>();
		if (!Spot)
		{
			return Out;
		}
		for (TActorIterator<AStorageActor> It(House->GetWorld()); It; ++It)
		{
			if (It->GetKind() == EStorageKind::FirewoodRack && !It->GetCarrier()
				&& FVector::Dist2D(It->GetActorLocation(), Spot->GetLocation()) <= WoodRackReach)
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	int32 WoodNearFire(const AApothecary* House)
	{
		int32 Total = 0;
		for (const AStorageActor* Rack : WoodRacksNear(House))
		{
			Total += Rack->CountStored(TEXT("firewood"));
		}
		return Total;
	}

	bool TakeWoodNearFire(const AApothecary* House)
	{
		for (AStorageActor* Rack : WoodRacksNear(House))
		{
			for (FInventorySlot& Slot : Rack->EditSlots())
			{
				if (!Slot.IsEmpty() && Slot.ItemId == TEXT("firewood"))
				{
					if (--Slot.Count <= 0)
					{
						Slot.Clear();
					}
					Rack->Commit();
					return true;
				}
			}
		}
		return false;
	}

	TArray<FRecipe> Recipes(const UItemDatabase& Items, ECraft Craft)
	{
		TArray<FRecipe> Out;
		const auto Add = [&Items, &Out, Craft](const TCHAR* Output, TArray<TPair<FName, int32>> Inputs)
		{
			// Seulement si tout existe dans le catalogue.
			bool bKnown = Items.Find(Output) != nullptr;
			for (const TPair<FName, int32>& Input : Inputs)
			{
				bKnown &= Items.Find(Input.Key) != nullptr;
			}
			if (bKnown)
			{
				Out.Add({ FName(Output), MoveTemp(Inputs), Craft });
			}
		};
		switch (Craft)
		{
		case ECraft::Mortar:
			// Chaque plante donne son écrasé.
			for (const FItemDef* Plant : Items.GetPlants())
			{
				Add(*Crushed(Plant->Id).ToString(), { { Plant->Id, 1 } });
			}
			break;
		case ECraft::Cauldron:
			break;   // le chaudron brasse des breuvages (ErewhonApothecary::Brews)
		case ECraft::Workbench:
			// Le pot-pourri, de fleurs et d'herbes séchées.
			Add(TEXT("potpourri"), { { TEXT("dried_flower_white"), 2 }, { TEXT("dried_flower_purple"), 2 }, { TEXT("dried_herb"), 1 } });
			// Les potions légères : une base et un écrasé ; les fortes : une base et quatre écrasés (et elles durent bien plus).
			for (const TPair<const TCHAR*, const TCHAR*>& Light : { TPair<const TCHAR*, const TCHAR*>(TEXT("speed"), TEXT("flower_blue")),
				{ TEXT("night"), TEXT("flower_purple") }, { TEXT("strength"), TEXT("weed_nettle") }, { TEXT("jump"), TEXT("flower_white") } })
			{
				Add(*FString::Printf(TEXT("potion_%s_light"), Light.Key), { { TEXT("potion_base"), 1 }, { Crushed(Light.Value), 1 } });
				Add(*FString::Printf(TEXT("potion_%s_strong"), Light.Key), { { TEXT("potion_base"), 1 }, { Crushed(Light.Value), 4 } });
			}
			// La tisane de sérénité : une base, un écrasé d'herbe aromatique.
			Add(TEXT("potion_calm"), { { TEXT("potion_base"), 1 }, { Crushed(TEXT("herb")), 1 } });
			// Les teintures : trois écrasés d'une même plante.
			Add(TEXT("dye_blue"), { { Crushed(TEXT("flower_blue")), 3 } });
			Add(TEXT("dye_purple"), { { Crushed(TEXT("flower_purple")), 3 } });
			Add(TEXT("dye_green"), { { Crushed(TEXT("weed_nettle")), 3 } });
			Add(TEXT("dye_rust"), { { Crushed(TEXT("weed_dock")), 3 } });
			// Les bouquets en pot : frais, ou de plantes séchées.
			Add(TEXT("bouquet_fresh"), { { TEXT("flower_white"), 2 }, { TEXT("flower_blue"), 2 }, { TEXT("flower_purple"), 1 }, { TEXT("fern"), 1 } });
			Add(TEXT("bouquet_dry"), { { TEXT("dried_flower_white"), 2 }, { TEXT("dried_flower_blue"), 1 }, { TEXT("dried_flower_purple"), 1 }, { TEXT("dried_grass"), 2 } });
			break;
		}
		return Out;
	}

	TOptional<FRecipe> FindRecipe(const UItemDatabase& Items, FName Output)
	{
		for (const ECraft Craft : { ECraft::Mortar, ECraft::Cauldron, ECraft::Workbench })
		{
			for (const FRecipe& Recipe : Recipes(Items, Craft))
			{
				if (Recipe.Output == Output)
				{
					return Recipe;
				}
			}
		}
		return {};
	}
}

// ── La maison ────────────────────────────────────────────────────────────────

AApothecary::AApothecary()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;   // une maison ne disparaît pas quand on s'éloigne
	SetNetUpdateFrequency(2.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AApothecary::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AApothecary, Sector);
	DOREPLIFETIME(AApothecary, FireUntil);
}

void AApothecary::Setup(const FIntPoint& InSector, double InFireUntil)
{
	Sector = InSector;
	FireUntil = InFireUntil;
}

bool AApothecary::IsFireLit() const
{
	return FireRemaining() > 0.0;
}

double AApothecary::FireRemaining() const
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	return State ? FMath::Max(0.0, FireUntil - State->GetWorldClock()) : 0.0;
}

void AApothecary::AddFire(double Seconds)
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (!State)
	{
		return;
	}
	const double Now = State->GetWorldClock();
	FireUntil = FMath::Min(FMath::Max(FireUntil, Now) + Seconds, Now + ErewhonApothecary::MaxFireSeconds);
	ForceNetUpdate();
	OnFireChanged.Broadcast();
}

void AApothecary::BeginPlay()
{
	Super::BeginPlay();
	BuildLocal();
}

void AApothecary::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Revente du secteur : la maison disparaît avec elle.
	if (AErewhonGameState* State = AErewhonGameState::Get(this))
	{
		State->OnSectorOwnerChanged.Remove(OwnerHandle);
	}
	if (House)
	{
		House->Destroy();
	}
	if (HearthStation)
	{
		HearthStation->Destroy();
	}
	if (Door && HasAuthority())
	{
		Door->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

void AApothecary::BuildHouse()
{
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	if (House)
	{
		House->Destroy();
	}
	bHouseHasStoreroom = ErewhonApothecary::HasStoreroom(State, Sector);
	const FTransform HouseT = GetActorTransform();
	House = GetWorld()->SpawnActorDeferred<ASpawnCabin>(ASpawnCabin::StaticClass(), HouseT, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	House->Kind = EShopKind::Apothecary;
	House->bStoreroom = bHouseHasStoreroom;
	House->FinishSpawning(HouseT);
}

void AApothecary::OnSectorOwnerChanged(const FIntPoint& Changed)
{
	// La remise vient d'être construite : la maison se refait avec sa porte de côté.
	if (Changed == Sector && bBuilt && ErewhonApothecary::HasStoreroom(AErewhonGameState::Get(this), Sector) != bHouseHasStoreroom)
	{
		BuildHouse();
	}
}

void AApothecary::BuildLocal()
{
	AErewhonGameState* State = AErewhonGameState::Get(this);
	if (bBuilt || !State)
	{
		return;
	}
	bBuilt = true;
	const FTransform HouseT = ErewhonApothecary::HouseTransform(Sector, State->GetWorldSeed());
	SetActorTransform(HouseT);
	BuildHouse();
	OwnerHandle = State->OnSectorOwnerChanged.AddUObject(this, &AApothecary::OnSectorOwnerChanged);

	// La porte (répliquée) : le serveur la pose.
	if (HasAuthority())
	{
		const FTransform Hinge = FTransform(FVector(-ErewhonSpawn::CabinHalfLength + ErewhonSpawn::CabinWallThickness * 0.5f,
			-ErewhonSpawn::DoorHalfWidth, ErewhonSpawn::CabinFloorTop)) * HouseT;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Door = GetWorld()->SpawnActor<ADoor>(Hinge.GetLocation(), Hinge.Rotator(), Params);
	}

	// Le foyer, qu'on vise pour y mettre du petit bois.
	{
		const FTransform At = FTransform(ErewhonApothecary::HearthLocal()) * HouseT;
		AApothecaryStation* Station = GetWorld()->SpawnActorDeferred<AApothecaryStation>(AApothecaryStation::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Station->Setup(this);
		Station->FinishSpawning(At);
		HearthStation = Station;
	}

	// Quelques grands arbres autour, sur les diagonales : jamais sur un chemin ni devant la porte.
	FRandomStream Rand(HashCombine(GetTypeHash(State->GetWorldSeed()), GetTypeHash(Sector)) ^ 0x51A7);
	TArray<UStaticMesh*> TreeModels;
	for (const TCHAR* Name : { TEXT("SM_tree_linden"), TEXT("SM_tree_oak") })
	{
		if (UStaticMesh* Model = LoadGenerated<UStaticMesh>(TEXT("Meshes"), Name))
		{
			TreeModels.Add(Model);
		}
	}
	const FVector2D Center(ErewhonSector::Center(Sector));
	for (int32 i = 0; i < 4 && TreeModels.Num() > 0; ++i)
	{
		const float Angle = FMath::DegreesToRadians(45.f + 90.f * i + Rand.FRandRange(-14.f, 14.f));
		const FVector2D Foot2D = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Rand.FRandRange(1250.f, 1500.f);
		const FVector Foot = HouseT.InverseTransformPosition(FVector(Foot2D, ErewhonTerrain::Height(Foot2D, State->GetWorldSeed()) - 10.f));
		AddGeneratedMesh(this, Root, TreeModels[i % TreeModels.Num()], Foot, Rand.FRandRange(900.f, 1150.f), Rand.FRandRange(0.f, 360.f), false);
		UStaticMeshComponent* Trunk = AddShape(this, Root, EShape::Cylinder, Foot + FVector(0, 0, 150), FVector(60, 60, 300), GBark, FRotator::ZeroRotator, true);
		Trunk->SetHiddenInGame(true);
	}

	// Le foyer : des bûches, toujours là ; flammes, lueur et crépitement quand le feu brûle.
	const FVector Hearth = ErewhonApothecary::HearthLocal();
	if (UStaticMesh* Logs = LoadGenerated<UStaticMesh>(TEXT("Meshes"), TEXT("SM_firewood")))
	{
		AddGeneratedMesh(this, Root, Logs, Hearth, 26.f, 90.f, false);
	}
	else
	{
		for (const float Yaw : { 20.f, -25.f, 80.f })
		{
			AddShape(this, Root, EShape::Cylinder, Hearth + FVector(0, 0, 8), FVector(11, 11, 60), GBark, FRotator(90.f, Yaw, 0.f), false);
		}
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Les flammes : des flammes peintes croisées, que M_Flame fait onduler, autour et sous le chaudron.
	UMaterialInterface* FlameMaterial = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_Flame"));
	UTexture* FlameTexture = LoadGenerated<UTexture>(TEXT("Textures"), TEXT("T_flame"));
	if (FlameMaterial && FlameTexture)
	{
		struct FTongue { FVector Offset; float Yaw; float Width; float Height; };
		const FTongue Tongues[] = {
			{ FVector(-6, 0, 12), 0.f, 50.f, 50.f }, { FVector(-2, 0, 10), 60.f, 44.f, 46.f }, { FVector(-2, 0, 10), 120.f, 44.f, 48.f },
			{ FVector(6, -24, 0), 20.f, 34.f, 48.f }, { FVector(0, 24, 0), 160.f, 34.f, 46.f }, { FVector(14, 4, 0), 90.f, 30.f, 44.f },
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Tongues); ++i)
		{
			USceneComponent* Pivot = NewObject<USceneComponent>(this);
			Pivot->SetupAttachment(Root);
			Pivot->SetRelativeLocation(Hearth + Tongues[i].Offset + FVector(0, 0, 2));
			Pivot->RegisterComponent();
			AddInstanceComponent(Pivot);
			if (UProceduralMeshComponent* Tongue = AddPicture(this, Pivot, FVector(0, 0, Tongues[i].Height * 0.5f), Tongues[i].Width, Tongues[i].Height, Tongues[i].Yaw, FlameTexture))
			{
				UMaterialInstanceDynamic* Glow = UMaterialInstanceDynamic::Create(FlameMaterial, this);
				Glow->SetScalarParameterValue(TEXT("Seed"), i * 0.37f);
				Tongue->SetMaterial(0, Glow);
				Tongue->SetCastShadow(false);
				FlameGlows.Add(Glow);
			}
			Pivot->SetVisibility(false, true);
			FlamePivots.Add(Pivot);
		}
	}
	FireLight = NewObject<UPointLightComponent>(this);
	FireLight->SetupAttachment(Root);
	FireLight->SetRelativeLocation(Hearth + FVector(-60.f, 0.f, 30.f));
	FireLight->SetIntensityUnits(ELightUnits::Lumens);
	FireLight->SetIntensity(0.f);
	FireLight->SetAttenuationRadius(900.f);
	FireLight->SetLightColor(GFireColor);
	FireLight->SetSourceRadius(20.f);
	FireLight->SetVisibility(false);
	FireLight->RegisterComponent();
	AddInstanceComponent(FireLight);

	if (USoundWave* Crackle = ErewhonSound::Load(TEXT("amb_fire")))
	{
		FireSound = NewObject<UAudioComponent>(this);
		FireSound->SetupAttachment(Root);
		FireSound->SetRelativeLocation(Hearth + FVector(0, 0, 30));
		FireSound->SetSound(Crackle);
		FireSound->AttenuationSettings = ErewhonSound::Attenuation(400.f, 1800.f);
		FireSound->SetVolumeMultiplier(0.f);
		FireSound->bAutoActivate = false;
		FireSound->RegisterComponent();
		AddInstanceComponent(FireSound);
	}
}

void AApothecary::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		BuildLocal();   // client : l'état du jeu est arrivé après la maison
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateFire(DeltaSeconds);
	}
	// Le feu qui faiblit se nourrit tout seul au rack à petit bois posé près de la cheminée.
	if (HasAuthority() && (RefuelTimer += DeltaSeconds) >= 1.f)
	{
		RefuelTimer = 0.f;
		if (IsFireLit() && FireRemaining() < 15.0 && ErewhonApothecary::TakeWoodNearFire(this))
		{
			AddFire(ErewhonApothecary::FireSecondsPerWood);
		}
	}
}

void AApothecary::UpdateFire(float DeltaSeconds)
{
	// Le feu prend et s'éteint en douceur ; la flamme vacille, la lumière avec elle.
	FireTime += DeltaSeconds;
	const float Target = IsFireLit() ? 1.f : 0.f;
	FireLevel = FMath::FInterpConstantTo(FireLevel, Target, DeltaSeconds, Target > FireLevel ? 0.6f : 0.25f);
	const bool bLit = FireLevel > 0.01f;
	const float Flicker = FMath::Clamp(1.f + 0.18f * FMath::PerlinNoise1D(FireTime * 2.3f) + 0.1f * FMath::PerlinNoise1D(FireTime * 11.f + 7.f), 0.6f, 1.4f);
	if (FireLight)
	{
		if (FireLight->IsVisible() != bLit)
		{
			FireLight->SetVisibility(bLit);
		}
		FireLight->SetIntensity(450.f * FireLevel * Flicker);
	}
	for (int32 i = 0; i < FlamePivots.Num(); ++i)
	{
		USceneComponent* Pivot = FlamePivots[i];
		if (Pivot->IsVisible() != bLit)
		{
			Pivot->SetVisibility(bLit, true);
		}
		if (bLit)
		{
			// Chaque flamme s'étire et retombe à son rythme ; le feu qui prend les fait grandir.
			const float Lick = 0.8f + 0.3f * FMath::PerlinNoise1D(FireTime * (3.f + i * 0.7f) + i * 13.f);
			const float Grow = FMath::Sqrt(FireLevel);
			Pivot->SetRelativeScale3D(FVector((0.9f + 0.1f * Lick) * Grow, (0.9f + 0.1f * Lick) * Grow, Lick * Grow));
		}
	}
	for (UMaterialInstanceDynamic* Glow : FlameGlows)
	{
		Glow->SetScalarParameterValue(TEXT("Intensity"), 14.f * Flicker);
	}
	if (FireSound)
	{
		if (bLit && !FireSound->IsPlaying())
		{
			FireSound->Play(FMath::FRandRange(0.f, 5.f));
		}
		else if (!bLit && FireSound->IsPlaying())
		{
			FireSound->Stop();
		}
		FireSound->SetVolumeMultiplier(1.4f * FireLevel * ErewhonSound::ChannelVolume(ErewhonSound::EChannel::Effects));
	}
}

// ── Le foyer ─────────────────────────────────────────────────────────────────

AApothecaryStation::AApothecaryStation()
{
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Bounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	Bounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	RootComponent = Bounds;
}

void AApothecaryStation::BeginPlay()
{
	Super::BeginPlay();
	// Les bûches seulement : le chaudron pendu au-dessus se vise à part.
	Bounds->SetBoxExtent(FVector(30.f, 55.f, 20.f));
	SetActorLocation(GetActorLocation() + GetActorRotation().RotateVector(FVector(-8.f, 0.f, 20.f)));
}

bool AApothecaryStation::IsLocalOwner() const
{
	const AApothecary* Home = House.Get();
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const AErewhonPlayerState* PS = PC ? PC->GetPlayerState<AErewhonPlayerState>() : nullptr;
	const AErewhonGameState* State = AErewhonGameState::Get(this);
	const FString* Key = Home && State ? State->FindSectorOwnerKey(Home->GetSector()) : nullptr;
	return Key && PS && *Key == PS->GetPlayerKey();
}

FText AApothecaryStation::GetInteractionLabel() const
{
	const AApothecary* Home = House.Get();
	if (!Home)
	{
		return FText::GetEmpty();
	}
	if (!IsLocalOwner())
	{
		const AErewhonGameState* State = AErewhonGameState::Get(this);
		return FText::Format(LOCTEXT("OthersHouse", "Apothicairerie de {0}"), FText::FromString(State ? State->GetSectorOwnerName(Home->GetSector()) : FString()));
	}
	const int32 RackWood = ErewhonApothecary::WoodNearFire(Home);
	if (Home->IsFireLit())
	{
		return RackWood > 0
			? FText::Format(LOCTEXT("FeedFireRack", "Remettre du petit bois (le feu brûle encore {0} min ; {1} brassées au rack, il s'y sert)"),
				FText::AsNumber(FMath::CeilToInt32(Home->FireRemaining() / 60.0)), FText::AsNumber(RackWood))
			: FText::Format(LOCTEXT("FeedFire", "Remettre du petit bois (le feu brûle encore {0} min)"),
				FText::AsNumber(FMath::CeilToInt32(Home->FireRemaining() / 60.0)));
	}
	if (RackWood > 0)
	{
		return FText::Format(LOCTEXT("LightFireRack", "Allumer le feu ({0} brassées au rack, à portée)"), FText::AsNumber(RackWood));
	}
	return LOCTEXT("LightFire", "Allumer le feu (petit bois)");
}

void AApothecaryStation::Interact(AErewhonPlayerController* Controller)
{
	AApothecary* Home = House.Get();
	if (!Controller || !Home)
	{
		return;
	}
	if (!IsLocalOwner())
	{
		Controller->ShowToast(LOCTEXT("NotYourHouse", "Ce n'est pas votre apothicairerie."));
		return;
	}
	Controller->ServerFeedFire(Home);
}

#undef LOCTEXT_NAMESPACE
