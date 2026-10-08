#include "World/ErewhonVisuals.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

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
}
