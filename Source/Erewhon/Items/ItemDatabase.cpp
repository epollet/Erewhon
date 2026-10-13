#include "Items/ItemDatabase.h"

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogErewhonItems, Log, All);

namespace
{
	template <typename TEnum>
	TEnum ParseEnum(const FString& Value, TEnum Fallback)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		const int64 Raw = Enum->GetValueByNameString(Value, EGetByNameFlags::CaseSensitive);
		if (Raw == INDEX_NONE)
		{
			// Le JSON est en snake_case ("torso"), les enums en PascalCase ("Torso").
			for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
			{
				if (Enum->GetNameStringByIndex(i).Equals(Value, ESearchCase::IgnoreCase))
				{
					return static_cast<TEnum>(Enum->GetValueByIndex(i));
				}
			}
			UE_LOG(LogErewhonItems, Warning, TEXT("Valeur inconnue '%s' pour %s"), *Value, *Enum->GetName());
			return Fallback;
		}
		return static_cast<TEnum>(Raw);
	}

	FLinearColor ParseColor(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FLinearColor& Fallback)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Obj->TryGetArrayField(Field, Values) || Values->Num() < 3)
		{
			return Fallback;
		}
		// Couleurs écrites en 0-255 sRGB, comme dans 3DSimEco.
		const FColor Srgb(
			static_cast<uint8>((*Values)[0]->AsNumber()),
			static_cast<uint8>((*Values)[1]->AsNumber()),
			static_cast<uint8>((*Values)[2]->AsNumber()));
		return FLinearColor::FromSRGBColor(Srgb);
	}

	FItemDef ParseItem(const TSharedPtr<FJsonObject>& Obj)
	{
		FItemDef Def;
		Def.Id = FName(Obj->GetStringField(TEXT("id")));
		Def.Name = FText::FromString(Obj->GetStringField(TEXT("name")));

		FString Text;
		if (Obj->TryGetStringField(TEXT("description"), Text))
		{
			Def.Description = FText::FromString(Text);
		}
		if (Obj->TryGetStringField(TEXT("category"), Text))
		{
			Def.Category = ParseEnum(Text, EItemCategory::Resource);
		}
		Obj->TryGetNumberField(TEXT("sell_price"), Def.SellPrice);
		Obj->TryGetNumberField(TEXT("buy_price"), Def.BuyPrice);
		Obj->TryGetNumberField(TEXT("max_stack"), Def.MaxStack);
		Def.MaxStack = FMath::Max(1, Def.MaxStack);
		Obj->TryGetBoolField(TEXT("harvest_tool"), Def.bHarvestTool);
		Def.Color = ParseColor(Obj, TEXT("color"), FLinearColor::White);

		if (Obj->TryGetStringField(TEXT("clothing_slot"), Text))
		{
			Def.ClothingSlot = ParseEnum(Text, EClothingSlot::None);
		}
		Obj->TryGetNumberField(TEXT("pocket_slots"), Def.PocketSlots);
		Obj->TryGetNumberField(TEXT("extra_equip_slots"), Def.ExtraEquipSlots);
		Obj->TryGetBoolField(TEXT("short_legs"), Def.bShortLegs);
		if (Obj->TryGetStringField(TEXT("potion_effect"), Text))
		{
			Def.PotionEffect = ParseEnum(Text, EPotionEffect::None);
		}

		if (Obj->TryGetStringField(TEXT("mesh"), Text))
		{
			Def.Mesh = FSoftObjectPath(Text);
		}
		if (Obj->TryGetStringField(TEXT("icon"), Text))
		{
			Def.Icon = FSoftObjectPath(Text);
		}
		const TArray<TSharedPtr<FJsonValue>>* Variants = nullptr;
		if (Obj->TryGetArrayField(TEXT("meshes"), Variants))
		{
			for (const TSharedPtr<FJsonValue>& Variant : *Variants)
			{
				Def.MeshVariants.Add(FSoftObjectPath(Variant->AsString()));
			}
		}
		if (Obj->TryGetStringField(TEXT("material"), Text))
		{
			Def.Material = FSoftObjectPath(Text);
		}

		// Conventions de Tools/Editor/import_generated.py : T_Icon_<id>, SM_<id>.
		const auto Generated = [](const TCHAR* Folder, const FString& Name)
		{
			const FString Package = FString::Printf(TEXT("/Game/Generated/%s/%s"), Folder, *Name);
			return FPackageName::DoesPackageExist(Package) ? FSoftObjectPath(Package + TEXT(".") + Name) : FSoftObjectPath();
		};
		const FString IdString = Def.Id.ToString();
		if (Def.Icon.IsNull())
		{
			Def.Icon = Generated(TEXT("Icons"), TEXT("T_Icon_") + IdString);
		}
		if (Def.Mesh.IsNull())
		{
			Def.Mesh = Generated(TEXT("Meshes"), TEXT("SM_") + IdString);
		}

		const TSharedPtr<FJsonObject>* PlantObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("plant"), PlantObj))
		{
			FPlantSpawnDef& Plant = Def.Plant;
			if ((*PlantObj)->TryGetStringField(TEXT("shape"), Text))
			{
				Plant.Shape = ParseEnum(Text, EPlantShape::Grass);
			}
			(*PlantObj)->TryGetNumberField(TEXT("height_min"), Plant.HeightMin);
			(*PlantObj)->TryGetNumberField(TEXT("height_max"), Plant.HeightMax);
			(*PlantObj)->TryGetNumberField(TEXT("cull_distance"), Plant.CullDistanceMeters);
			(*PlantObj)->TryGetNumberField(TEXT("seed_salt"), Plant.SeedSalt);
			(*PlantObj)->TryGetNumberField(TEXT("cluster_radius"), Plant.ClusterRadiusMeters);
			const TArray<TSharedPtr<FJsonValue>>* Cluster = nullptr;
			if ((*PlantObj)->TryGetArrayField(TEXT("cluster"), Cluster) && Cluster->Num() == 2)
			{
				Plant.ClusterMin = FMath::Max(1, static_cast<int32>((*Cluster)[0]->AsNumber()));
				Plant.ClusterMax = FMath::Max(Plant.ClusterMin, static_cast<int32>((*Cluster)[1]->AsNumber()));
			}
			const TArray<TSharedPtr<FJsonValue>>* Patch = nullptr;
			if ((*PlantObj)->TryGetArrayField(TEXT("patch"), Patch) && Patch->Num() == 2)
			{
				Plant.PatchRadiusMeters = FMath::Max(0.f, static_cast<float>((*Patch)[0]->AsNumber()));
				Plant.PatchSize = FMath::Max(1, static_cast<int32>((*Patch)[1]->AsNumber()));
			}
			Plant.AccentColor = ParseColor(*PlantObj, TEXT("accent_color"), FLinearColor::Yellow);

			const TSharedPtr<FJsonObject>* Counts = nullptr;
			if ((*PlantObj)->TryGetObjectField(TEXT("count"), Counts))
			{
				for (const auto& Pair : (*Counts)->Values)
				{
					const ESectorType Type = ParseEnum(FString(Pair.Key), ESectorType::Undefined);
					if (Type != ESectorType::Undefined)
					{
						Plant.CountPerSector.Add(Type, static_cast<int32>(Pair.Value->AsNumber()));
					}
				}
			}
		}
		return Def;
	}
}

void UItemDatabase::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadFromJson(FPaths::ProjectContentDir() / TEXT("Data/Items.json"));
}

UItemDatabase* UItemDatabase::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UItemDatabase>() : nullptr;
}

const FItemDef* UItemDatabase::Find(FName Id) const
{
	const int32* Index = IndexById.Find(Id);
	return Index ? &Items[*Index] : nullptr;
}

void UItemDatabase::LoadFromJson(const FString& Path)
{
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		UE_LOG(LogErewhonItems, Error, TEXT("Catalogue introuvable : %s"), *Path);
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogErewhonItems, Error, TEXT("JSON invalide : %s"), *Path);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Root->TryGetArrayField(TEXT("items"), Entries))
	{
		UE_LOG(LogErewhonItems, Error, TEXT("Champ 'items' manquant dans %s"), *Path);
		return;
	}

	Items.Reset();
	IndexById.Reset();
	for (const TSharedPtr<FJsonValue>& Entry : *Entries)
	{
		const TSharedPtr<FJsonObject> Obj = Entry->AsObject();
		if (!Obj.IsValid())
		{
			continue;
		}
		FItemDef Def = ParseItem(Obj);
		if (!Def.IsValid() || IndexById.Contains(Def.Id))
		{
			UE_LOG(LogErewhonItems, Warning, TEXT("Objet ignoré (id vide ou en double) : %s"), *Def.Id.ToString());
			continue;
		}
		IndexById.Add(Def.Id, Items.Num());
		Items.Add(MoveTemp(Def));
	}

	// Les pointeurs sont pris après remplissage : Items ne bouge plus.
	Plants.Reset();
	Purchasable.Reset();
	for (const FItemDef& Def : Items)
	{
		if (Def.Category == EItemCategory::Plant)
		{
			Plants.Add(&Def);
		}
		if (Def.BuyPrice > 0)
		{
			Purchasable.Add(&Def);
		}
	}
	UE_LOG(LogErewhonItems, Log, TEXT("%d objets chargés (%d plantes, %d en boutique)"), Items.Num(), Plants.Num(), Purchasable.Num());
}
