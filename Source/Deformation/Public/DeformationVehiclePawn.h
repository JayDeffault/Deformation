// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DeformationComponent.h"
#include "GameFramework/Pawn.h"
#include "DeformationVehiclePawn.generated.h"

class UPoseableMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;

/**
 * Ready-to-use pawn for skeletal vehicle deformation.
 *
 * Put your Skeletal Mesh asset on TargetMesh, set RootBone, and the pawn wires the collision mesh, visible poseable
 * mesh, and deformation component together automatically. TargetMesh is the root component so skeletal physics starts
 * at the ADeformationVehiclePawn transform instead of being simulated as a detached child component.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Deformation))
class DEFORMATION_API ADeformationVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	ADeformationVehiclePawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Helper scene component kept for Blueprint organization. TargetMesh is the actual root for stable skeletal physics. */
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

	/** If true, deformation directions are forced toward the mesh center instead of stretching outward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction")
	bool bForceInwardDeformation = true;

	/** Use each bone's local normal axis as inward deformation direction for hedgehog-style bones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction")
	bool bUseBoneNormalForDeformation = true;

	/** Local bone axis used as inward deformation normal when bUseBoneNormalForDeformation is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction", meta = (EditCondition = "bUseBoneNormalForDeformation"))
	EDeformationBoneNormalAxis BoneNormalAxis = EDeformationBoneNormalAxis::NegativeX;

	/** If true, each accepted hit adds dent depth up to MaxOffset and opposite hits cannot push it back out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction")
	bool bAccumulateHitsToMaxOffset = true;

	/** If true, the matching PHAT body is moved together with the visual deformation offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bMovePhysicsBodyWithDeformation = true;

	/** Prefer kinematic deformation helper bodies when a hit is reported on a simulated door/hinge/chassis body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bPreferKinematicBodiesForDeformation = true;

	/** Collision profile applied to TargetMesh so Physics Asset bodies are visible/usable for blocking hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	FName CollisionProfileName = TEXT("PhysicsActor");

	/** Force the TargetMesh object type and all collision responses to block so PHAT bodies are easy to verify/debug. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bForceBlockingPhysicsCollision = true;

	/** Keep all bodies below RootBone kinematic while RootBone remains simulated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bUseKinematicPhysicsBodies = true;

	/** Enable skeletal physics simulation. With kinematic bodies enabled, only RootBone remains simulated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bSimulatePhysics = true;

	/** Enable gravity on TargetMesh when full skeletal physics simulation is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bEnableGravity = true;

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
	void AlignKinematicBodiesToCurrentBones();
	FName GetEffectiveSimulationRootBone() const;
	FName FindNearestSimulatedParentBone(FName BoneName) const;

	UPROPERTY(Transient)
	TMap<FName, FTransform> InitialKinematicBodyRelativeToSimulatedParent;
};
