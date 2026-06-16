// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeformationComponent.generated.h"

class AActor;
class USkeletalMeshComponent;
class UPoseableMeshComponent;
class UPrimitiveComponent;

/** Per-bone tuning for collision driven deformation. */
USTRUCT(BlueprintType)
struct DEFORMATION_API FDeformationBoneSettings
{
	GENERATED_BODY()

	/** Physics body / skeleton bone that receives deformation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation")
	FName BoneName = NAME_None;

	/** Ignore hits weaker than this normal impulse, in kg*cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (ClampMin = "0.0"))
	float MinImpulse = 50000.0f;

	/** Impulse mapped to MaxOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (ClampMin = "1.0"))
	float ImpulseForMaxOffset = 1200000.0f;

	/** Maximum permanent offset accumulated for this bone, in component-space centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (ClampMin = "0.0"))
	float MaxOffset = 18.0f;

	/** Use DeformationDirectionCS instead of deriving the dent direction from the hit normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction")
	bool bUseCustomDeformationDirection = false;

	/** If true, this bone accumulates only along one direction and cannot be pushed back. Disabled by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction")
	bool bLockDeformationDirection = false;

	/** One-way component-space dent direction. Offsets only accumulate in this direction and never push back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction", meta = (EditCondition = "bUseCustomDeformationDirection"))
	FVector DeformationDirectionCS = FVector::ForwardVector;

	/** Multiplier applied to generated physics impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (ClampMin = "0.0"))
	float PhysicsImpulseScale = 1.0f;
};

/** Runtime state for a deformed bone. OffsetCS is also applied directly when direct bone transforms are enabled. */
USTRUCT(BlueprintType)
struct DEFORMATION_API FDeformationBoneState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	FName BoneName = NAME_None;

	/** Accumulated component-space translation offset in centimeters. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	FVector OffsetCS = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	float LastImpulse = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	FVector LastHitLocationWS = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnBoneDeformedSignature, FName, BoneName, FVector, OffsetCS, float, Impulse);

/**
 * Listens for hits on a skeletal vehicle mesh and converts collision impulse into per-bone deformation offsets.
 *
 * The component also kicks the matching physics body away from the impact so fully simulated deformation bones can
 * move immediately. It can either expose offsets for an animation graph or apply them directly to a PoseableMeshComponent.
 */
UCLASS(ClassGroup = (Deformation), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class DEFORMATION_API UDeformationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeformationComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Skeletal mesh to observe. If empty, the owner's first SkeletalMeshComponent is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation")
	TObjectPtr<USkeletalMeshComponent> TargetMesh;

	/** If true, hits on bones that are not listed in BoneSettings are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation")
	bool bOnlyConfiguredBones = false;

	/** Root / chassis bone that must never receive deformation offsets or generated deformation impulses. Everything else deforms automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation")
	FName RootBone = NAME_None;

	/** If true, the component writes bone locations directly to a PoseableMeshComponent, so no Anim Blueprint or Control Rig is required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direct Bones")
	bool bApplyDirectBoneTransforms = true;

	/** Poseable visual mesh that receives direct bone offsets. If empty, it can be created automatically from TargetMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direct Bones")
	TObjectPtr<UPoseableMeshComponent> PoseableMesh;

	/** Automatically create a PoseableMeshComponent copy of TargetMesh at runtime when direct bone transforms are enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direct Bones")
	bool bAutoCreatePoseableMesh = true;

	/** Hide TargetMesh rendering while keeping its collision/physics active; PoseableMesh becomes the visible deformed mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direct Bones")
	bool bHideTargetMeshWhenUsingPoseable = true;

	/** Copy TargetMesh pose before reapplying offsets. Disable only if PoseableMesh is controlled entirely by this component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direct Bones")
	bool bCopyTargetPoseBeforeApplyingDirectOffsets = true;

	/** If true, AddImpulse is called on the impacted physics body as well as storing the deformation offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bApplyPhysicsImpulse = true;

	/** If true, the PHAT body bound to the hit bone is teleported inward by the same accepted deformation delta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bMovePhysicsBodyWithDeformation = true;

	/** If true, only kinematic PHAT bodies can receive deformation; disabled by default so every hit bone can dent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bDeformOnlyKinematicBodies = false;

	/** If true, generated AddImpulse uses velocity change mode and is independent from body mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bVelocityChange = false;

	/** Estimate impact impulse from relative velocity when kinematic PHAT bodies report zero NormalImpulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bEstimateKinematicHitImpulse = true;

	/** Relative speed multiplier used when NormalImpulse is zero for kinematic PHAT hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics", meta = (ClampMin = "0.0"))
	float KinematicHitImpulseScale = 250.0f;

	/** If true, hit normals are flipped toward mesh center so dents go inward instead of stretching outward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction")
	bool bForceInwardDeformation = true;

	/** If true, repeated hits add depth up to MaxOffset and opposite hits cannot push the dent back out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Direction")
	bool bAccumulateHitsToMaxOffset = true;

	/** Optional automatic interpolation back to zero; 0 keeps dents permanently until ResetDeformation is called. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (ClampMin = "0.0"))
	float RecoverySpeed = 0.0f;

	/** Default settings used when bOnlyConfiguredBones is false and a hit bone has no explicit entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation")
	FDeformationBoneSettings DefaultBoneSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation")
	TArray<FDeformationBoneSettings> BoneSettings;

	UPROPERTY(BlueprintAssignable, Category = "Deformation")
	FOnBoneDeformedSignature OnBoneDeformed;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Deformation")
	void BindToMesh(USkeletalMeshComponent* MeshComponent);

	UFUNCTION(BlueprintCallable, Category = "Deformation|Direct Bones")
	void SetPoseableMesh(UPoseableMeshComponent* MeshComponent);

	/** Creates/configures the poseable visual mesh used for direct C++ bone movement. */
	UFUNCTION(BlueprintCallable, Category = "Deformation|Direct Bones")
	bool InitializeDirectBoneTransforms();

	/** Reapplies every stored deformation offset to PoseableMesh. */
	UFUNCTION(BlueprintCallable, Category = "Deformation|Direct Bones")
	bool RefreshDirectBoneTransforms();

	UFUNCTION(BlueprintCallable, Category = "Deformation")
	void ResetDeformation(FName BoneName = NAME_None);

	/** Returns true when BoneName is the configured RootBone and must not deform. */
	UFUNCTION(BlueprintPure, Category = "Deformation")
	bool IsRootBone(FName BoneName) const;

	UFUNCTION(BlueprintPure, Category = "Deformation")
	FVector GetBoneDeformationOffset(FName BoneName) const;

	UFUNCTION(BlueprintPure, Category = "Deformation")
	bool GetBoneDeformationState(FName BoneName, FDeformationBoneState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Deformation")
	void GetAllDeformationStates(TArray<FDeformationBoneState>& OutStates) const;

	/** Manually add a deformation impulse, useful for traces or custom damage systems. */
	UFUNCTION(BlueprintCallable, Category = "Deformation")
	bool ApplyDeformationImpulse(FName BoneName, const FVector& HitLocationWS, const FVector& HitNormalWS, float NormalImpulse);

private:
	UFUNCTION()
	void HandleMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	const FDeformationBoneSettings* FindSettings(FName BoneName) const;
	FName ResolveHitBone(const FHitResult& Hit) const;
	FName FindClosestDeformableBody(const FVector& HitLocationWS) const;
	bool CanDeformPhysicsBody(FName BoneName) const;
	FDeformationBoneState& FindOrAddState(FName BoneName);
	FVector ResolveInwardDeformationDirection(const FVector& HitLocationWS, const FVector& HitNormalWS) const;
	void ApplyPhysicsBodyTransformsToPoseable() const;
	void ApplyDirectOffsetToPoseableBone(FName BoneName, const FVector& OffsetCS) const;
	void MovePhysicsBodyByOffset(FName BoneName, const FVector& OffsetWS) const;
	void RemoveRootBoneState();

	UPROPERTY(Transient)
	TMap<FName, FDeformationBoneState> BoneStates;
};
