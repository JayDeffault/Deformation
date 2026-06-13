// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DeformationVehiclePawn.generated.h"

class UDeformationComponent;
class UPoseableMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;

/**
 * Ready-to-use pawn for skeletal vehicle deformation.
 *
 * Put your Skeletal Mesh asset on TargetMesh, set RootBone, and the pawn wires the collision mesh, visible poseable
 * mesh, and deformation component together automatically. TargetMesh and PoseableMesh are children of PawnRoot so they
 * inherit the ADeformationVehiclePawn transform.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Deformation))
class DEFORMATION_API ADeformationVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	ADeformationVehiclePawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Root scene component. Move/rotate/scale the pawn; TargetMesh and PoseableMesh follow this transform. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	TObjectPtr<USceneComponent> PawnRoot;

	/** Collision/physics-asset skeletal mesh. Assign your vehicle Skeletal Mesh here. */
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

	/** Collision profile applied to TargetMesh so Physics Asset bodies are visible/usable for blocking hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	FName CollisionProfileName = TEXT("PhysicsActor");

	/** Force the TargetMesh object type and all collision responses to block so PHAT bodies are easy to verify/debug. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bForceBlockingPhysicsCollision = true;

	/** Keep PHAT bodies kinematic by default: they collide and report hits, but the pawn transform drives the vehicle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bUseKinematicPhysicsBodies = true;

	/** Enable full skeletal physics simulation instead of kinematic PHAT bodies. Leave false for vehicle dent deformation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bSimulatePhysics = false;

	/** Wake skeletal bodies after physics is configured so hit events and impulses start working immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bWakeRigidBodies = true;

	/** Push current pawn defaults into meshes and DeformationComponent. */
	UFUNCTION(BlueprintCallable, Category = "Deformation")
	void ConfigureDeformation();

private:
	void ConfigureTargetMeshTransform();
	void ConfigureTargetMeshCollisionAndPhysics();
	void ConfigurePoseableMeshTransform();
};
