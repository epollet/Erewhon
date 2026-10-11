#include "World/ErewhonVisuals.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "ProceduralMeshComponent.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace ErewhonVisuals
{
	UStaticMesh* GetShape(EShape Shape)
	{
		const TCHAR* Path = nullptr;
		switch (Shape)
		{
		case EShape::Cube:     Path = TEXT("/Engine/BasicShapes/Cube.Cube"); break;
		case EShape::Cylinder: Path = TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); break;
		case EShape::Cone:     Path = TEXT("/Engine/BasicShapes/Cone.Cone"); break;
		case EShape::Sphere:   Path = TEXT("/Engine/BasicShapes/Sphere.Sphere"); break;
		case EShape::Plane:    Path = TEXT("/Engine/BasicShapes/Plane.Plane"); break;
		}
		return LoadObject<UStaticMesh>(nullptr, Path);
	}

	UMaterialInstanceDynamic* MakeColorMaterial(UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Outer);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		return Material;
	}

	UStaticMeshComponent* AddShape(AActor* Owner, USceneComponent* Parent, EShape Shape,
		const FVector& Location, const FVector& Size, const FLinearColor& Color,
		const FRotator& Rotation, bool bCollision)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
		Component->SetStaticMesh(GetShape(Shape));
		Component->SetMaterial(0, MakeColorMaterial(Component, Color));
		Component->SetupAttachment(Parent);

		// Le cône de base a son pivot au centre ; on le ramène à sa base.
		FVector Offset = FVector::ZeroVector;
		if (Shape == EShape::Cone)
		{
			Offset = Rotation.RotateVector(FVector(0, 0, Size.Z * 0.5f));
		}
		Component->SetRelativeLocationAndRotation(Location + Offset, Rotation);
		Component->SetRelativeScale3D(Size / 100.f);
		Component->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Component->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Component->RegisterComponent();
		Owner->AddInstanceComponent(Component);
		return Component;
	}

	UMaterialInterface* MakePaintedMaterial(UObject* Outer, const TCHAR* TextureName, float TileSize,
		const FLinearColor& Fallback, const FLinearColor& Tint)
	{
		UMaterialInterface* Base = LoadGenerated<UMaterialInterface>(TEXT("Materials"), TEXT("M_PaintedWorld"));
		UTexture* Texture = LoadGenerated<UTexture>(TEXT("Textures"), FString::Printf(TEXT("T_%s"), TextureName));
		if (!Base || !Texture)
		{
			return MakeColorMaterial(Outer, Fallback);
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Outer);
		Material->SetTextureParameterValue(TEXT("Albedo"), Texture);
		Material->SetScalarParameterValue(TEXT("TileSize"), TileSize);
		Material->SetVectorParameterValue(TEXT("Tint"), Tint);
		return Material;
	}

	/**
	 * Dans l'éditeur, un mesh fraîchement importé peut encore compiler : ses dimensions
	 * seraient fausses. On attend la fin de sa compilation avant de le mesurer.
	 */
	static void EnsureCompiled(const UStaticMesh* Mesh)
	{
#if WITH_EDITOR
		if (Mesh && Mesh->IsCompiling())
		{
			FStaticMeshCompilingManager::Get().FinishCompilation({ const_cast<UStaticMesh*>(Mesh) });
		}
#endif
	}

	FTransform FitToHeight(const UStaticMesh* Mesh, float HeightCm)
	{
		EnsureCompiled(Mesh);
		const FBox Box = Mesh->GetBoundingBox();
		const float Height = FMath::Max(Box.GetSize().Z, 1.f);
		const float Scale = HeightCm / Height;
		const FVector Center = Box.GetCenter();
		return FTransform(FRotator::ZeroRotator, FVector(-Center.X, -Center.Y, -Box.Min.Z) * Scale, FVector(Scale));
	}

	FTransform FitToLength(const UStaticMesh* Mesh, float LengthCm, float HeightCm)
	{
		EnsureCompiled(Mesh);
		const FBox Box = Mesh->GetBoundingBox();
		const FVector Size = Box.GetSize();
		const bool bLongAlongX = Size.X > Size.Y;
		const float Scale = LengthCm / FMath::Max(FMath::Max(Size.X, Size.Y), 1.f);
		const float ScaleZ = HeightCm > 0.f ? HeightCm / FMath::Max(Size.Z, 1.f) : Scale;
		const FVector Center = Box.GetCenter();
		const FTransform Centered(FRotator::ZeroRotator, FVector(-Center.X * Scale, -Center.Y * Scale, -Box.Min.Z * ScaleZ), FVector(Scale, Scale, ScaleZ));
		// Un quart de tour ramène le grand côté sur Y.
		return Centered * FTransform(FRotator(0.f, bLongAlongX ? 90.f : 0.f, 0.f));
	}

	UProceduralMeshComponent* AddPrism(AActor* Owner, USceneComponent* Parent,
		const TArray<FVector2D>& PolygonYZ, float X0, float X1, UMaterialInterface* Material)
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		const int32 N = PolygonYZ.Num();

		// Faces avant (X1, normale +X) et arrière (X0, normale −X), en éventail.
		for (const float X : { X0, X1 })
		{
			const int32 Start = Vertices.Num();
			const bool bFront = X == X1;
			for (const FVector2D& P : PolygonYZ)
			{
				Vertices.Add(FVector(X, P.X, P.Y));
				Normals.Add(FVector(bFront ? 1.f : -1.f, 0.f, 0.f));
			}
			for (int32 i = 1; i + 1 < N; ++i)
			{
				Triangles.Append(bFront ? TArray<int32>{ Start, Start + i + 1, Start + i } : TArray<int32>{ Start, Start + i, Start + i + 1 });
			}
		}
		// Côtés : un quad par arête, normale sortante.
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D A = PolygonYZ[i];
			const FVector2D B = PolygonYZ[(i + 1) % N];
			const FVector Edge(0.f, B.X - A.X, B.Y - A.Y);
			const FVector Normal = FVector(0.f, Edge.Z, -Edge.Y).GetSafeNormal();
			const int32 Start = Vertices.Num();
			Vertices.Append({ FVector(X0, A.X, A.Y), FVector(X1, A.X, A.Y), FVector(X1, B.X, B.Y), FVector(X0, B.X, B.Y) });
			Normals.Append({ Normal, Normal, Normal, Normal });
			Triangles.Append({ Start, Start + 2, Start + 1, Start, Start + 3, Start + 2 });
		}

		UProceduralMeshComponent* Prism = NewObject<UProceduralMeshComponent>(Owner);
		Prism->SetupAttachment(Parent);
		Prism->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, {}, {}, {}, true);
		Prism->SetMaterial(0, Material);
		Prism->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Prism->RegisterComponent();
		Owner->AddInstanceComponent(Prism);
		return Prism;
	}

	UProceduralMeshComponent* AddLoft(AActor* Owner, USceneComponent* Parent,
		const TArray<FVector2D>& Bottom, float BottomZ, const TArray<FVector2D>& Top, float TopZ, UMaterialInterface* Material)
	{
		check(Bottom.Num() == Top.Num() && Top.Num() >= 3);
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		const int32 N = Top.Num();

		// Dessus : éventail depuis le centre du contour.
		FVector2D Middle = FVector2D::ZeroVector;
		for (const FVector2D& P : Top)
		{
			Middle += P / N;
		}
		Vertices.Add(FVector(Middle, TopZ));
		Normals.Add(FVector::UpVector);
		for (const FVector2D& P : Top)
		{
			Vertices.Add(FVector(P, TopZ));
			Normals.Add(FVector::UpVector);
		}
		for (int32 i = 0; i < N; ++i)
		{
			Triangles.Append({ 0, 1 + (i + 1) % N, 1 + i });
		}

		// Flancs : un quad par arête, normale sortante.
		for (int32 i = 0; i < N; ++i)
		{
			const int32 j = (i + 1) % N;
			const FVector A(Bottom[i], BottomZ), B(Bottom[j], BottomZ), C(Top[j], TopZ), D(Top[i], TopZ);
			const FVector Normal = FVector::CrossProduct(B - A, D - A).GetSafeNormal();   // vers l'extérieur
			const int32 Start = Vertices.Num();
			Vertices.Append({ A, B, C, D });
			Normals.Append({ Normal, Normal, Normal, Normal });
			Triangles.Append({ Start, Start + 2, Start + 1, Start, Start + 3, Start + 2 });
		}

		UProceduralMeshComponent* Loft = NewObject<UProceduralMeshComponent>(Owner);
		Loft->SetupAttachment(Parent);
		Loft->bUseAsyncCooking = false;
		Loft->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, {}, {}, {}, true);
		Loft->SetMaterial(0, Material);
		Loft->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Loft->RegisterComponent();
		Owner->AddInstanceComponent(Loft);
		return Loft;
	}

	UStaticMeshComponent* AddGeneratedMesh(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh,
		const FVector& Location, float HeightCm, float Yaw, bool bCollision)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
		Component->SetStaticMesh(Mesh);
		Component->SetupAttachment(Parent);
		const FTransform Fit = FitToHeight(Mesh, HeightCm);
		const FTransform Placement(FRotator(0.f, Yaw, 0.f), Location);
		Component->SetRelativeTransform(Fit * Placement);
		Component->SetCollisionProfileName(bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Component->RegisterComponent();
		Owner->AddInstanceComponent(Component);
		return Component;
	}
}
