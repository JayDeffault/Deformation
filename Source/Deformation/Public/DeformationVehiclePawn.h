// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DeformationVehiclePawn.generated.h"

class UDeformationComponent;
class UPoseableMeshComponent;
class USkeletalMeshComponent;

/**
 * Ready-to-use pawn for skeletal vehicle deformation.
 *
 * Put your Skeletal Mesh asset on TargetMesh (the RootComponent), set RootBone, and the pawn wires the collision mesh,
 * visible poseable mesh, and deformation component together automatically.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Deformation))
class DEFORMATION_API ADeformationVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	ADeformationVehiclePawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Collision/physics skeletal mesh. This is the RootComponent; assign your vehicle Skeletal Mesh here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	TObjectPtr<USkeletalMeshComponent> TargetMesh;

	/** Visible mesh whose bones are moved directly by the deformation component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	TObjectPtr<UPoseableMeshComponent> PoseableMesh;

	/** Collision-driven deformation logic. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	TObjectPtr<UDeformationComponent> DeformationComponent;

	/** Root / chassis bone that must stay fixed and never receive deformation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation")
	FName RootBone = NAME_None;

	/** Push current pawn defaults into DeformationComponent and refresh the poseable mesh. */
	UFUNCTION(BlueprintCallable, Category = "Deformation")
	void ConfigureDeformation();
};
