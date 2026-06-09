// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeformationComponent.generated.h"

class AActor;
class USkeletalMeshComponent;
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

	/** Multiplier applied to generated physics impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation", meta = (ClampMin = "0.0"))
	float PhysicsImpulseScale = 1.0f;
};

/** Runtime state for a deformed bone. Feed OffsetCS into Control Rig or an Anim Blueprint Transform Bone node. */
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
 * move immediately. For visual meshes driven by an Anim Blueprint / Control Rig, query GetBoneDeformationOffset().
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
	bool bOnlyConfiguredBones = true;

	/** If true, AddImpulse is called on the impacted physics body as well as storing the deformation offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bApplyPhysicsImpulse = true;

	/** If true, generated AddImpulse uses velocity change mode and is independent from body mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformation|Physics")
	bool bVelocityChange = false;

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

	UFUNCTION(BlueprintCallable, Category = "Deformation")
	void ResetDeformation(FName BoneName = NAME_None);

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
	FDeformationBoneState& FindOrAddState(FName BoneName);

	UPROPERTY(Transient)
	TMap<FName, FDeformationBoneState> BoneStates;
};
