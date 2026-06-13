// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeformationVehiclePawn.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DeformationComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

ADeformationVehiclePawn::ADeformationVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	TargetMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TargetMesh"));
	SetRootComponent(TargetMesh);
	TargetMesh->SetMobility(EComponentMobility::Movable);
	TargetMesh->SetRelativeTransform(FTransform::Identity);
	TargetMesh->SetCollisionProfileName(CollisionProfileName);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetEnableGravity(bEnableGravity);
	TargetMesh->SetNotifyRigidBodyCollision(true);
	TargetMesh->SetGenerateOverlapEvents(false);

	PawnRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PawnRoot"));
	PawnRoot->SetupAttachment(TargetMesh);
	PawnRoot->SetMobility(EComponentMobility::Movable);
	PawnRoot->SetRelativeTransform(FTransform::Identity);

	PoseableMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("PoseableMesh"));
	PoseableMesh->SetupAttachment(TargetMesh);
	PoseableMesh->SetMobility(EComponentMobility::Movable);
	PoseableMesh->SetRelativeTransform(FTransform::Identity);
	PoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseableMesh->SetGenerateOverlapEvents(false);
	PoseableMesh->SetCastShadow(true);

	DeformationComponent = CreateDefaultSubobject<UDeformationComponent>(TEXT("DeformationComponent"));
	DeformationComponent->TargetMesh = TargetMesh;
	DeformationComponent->PoseableMesh = PoseableMesh;
	DeformationComponent->bApplyDirectBoneTransforms = true;
	DeformationComponent->bAutoCreatePoseableMesh = false;
	DeformationComponent->bHideTargetMeshWhenUsingPoseable = true;
	DeformationComponent->bOnlyConfiguredBones = false;
	DeformationComponent->bApplyPhysicsImpulse = !bUseKinematicPhysicsBodies;
}

void ADeformationVehiclePawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureDeformation();
}

void ADeformationVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	ConfigureDeformation();
}

void ADeformationVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void ADeformationVehiclePawn::ConfigureDeformation()
{
	ConfigureTargetMeshTransform();
	ConfigureTargetMeshCollisionAndPhysics();
	ConfigurePoseableMeshTransform();

	if (!DeformationComponent)
	{
		return;
	}

	DeformationComponent->TargetMesh = TargetMesh;
	DeformationComponent->PoseableMesh = PoseableMesh;
	DeformationComponent->RootBone = RootBone;
	DeformationComponent->bApplyDirectBoneTransforms = true;
	DeformationComponent->bAutoCreatePoseableMesh = false;
	DeformationComponent->bHideTargetMeshWhenUsingPoseable = true;
	DeformationComponent->bOnlyConfiguredBones = false;
	DeformationComponent->bApplyPhysicsImpulse = !bUseKinematicPhysicsBodies;

	DeformationComponent->BindToMesh(TargetMesh);
	DeformationComponent->SetPoseableMesh(PoseableMesh);
}

void ADeformationVehiclePawn::ConfigureTargetMeshTransform()
{
	if (!TargetMesh)
	{
		return;
	}

	TargetMesh->SetMobility(EComponentMobility::Movable);
	if (RootComponent != TargetMesh)
	{
		SetRootComponent(TargetMesh);
	}

	TargetMesh->SetWorldTransform(GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
}

void ADeformationVehiclePawn::ConfigureTargetMeshCollisionAndPhysics()
{
	if (!TargetMesh)
	{
		return;
	}

	TargetMesh->SetCollisionProfileName(CollisionProfileName);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetEnableGravity(bEnableGravity);
	TargetMesh->SetNotifyRigidBodyCollision(true);
	TargetMesh->SetAllBodiesNotifyRigidBodyCollision(true);
	TargetMesh->SetGenerateOverlapEvents(false);

	if (bForceBlockingPhysicsCollision)
	{
		TargetMesh->SetCollisionObjectType(ECC_PhysicsBody);
		TargetMesh->SetCollisionResponseToAllChannels(ECR_Block);
	}

	TargetMesh->SetEnableGravity(bEnableGravity);

	if (bSimulatePhysics && bUseKinematicPhysicsBodies)
	{
		TargetMesh->SetSimulatePhysics(true);
	}
	else
	{
		TargetMesh->SetSimulatePhysics(bSimulatePhysics);
		TargetMesh->SetAllBodiesSimulatePhysics(bSimulatePhysics);
	}

	AlignKinematicBodiesToCurrentBones();

	if (bWakeRigidBodies)
	{
		TargetMesh->WakeAllRigidBodies();
	}
}

void ADeformationVehiclePawn::ConfigurePoseableMeshTransform()
{
	if (!PoseableMesh)
	{
		return;
	}

	PoseableMesh->SetMobility(EComponentMobility::Movable);
	USceneComponent* MeshAttachParent = (bSimulatePhysics && TargetMesh)
		? static_cast<USceneComponent*>(TargetMesh.Get())
		: static_cast<USceneComponent*>(PawnRoot ? PawnRoot.Get() : RootComponent.Get());
	if (MeshAttachParent)
	{
		PoseableMesh->AttachToComponent(MeshAttachParent, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}
	PoseableMesh->SetRelativeTransform(FTransform::Identity);
	PoseableMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseableMesh->SetGenerateOverlapEvents(false);
	PoseableMesh->SetCastShadow(true);
}

void ADeformationVehiclePawn::AlignKinematicBodiesToCurrentBones()
{
	if (!TargetMesh || !bUseKinematicPhysicsBodies)
	{
		return;
	}

	UPhysicsAsset* PhysicsAsset = TargetMesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	const FName SimulationRootBone = GetEffectiveSimulationRootBone();
	for (USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!BodySetup || BodySetup->BoneName.IsNone() || BodySetup->BoneName == SimulationRootBone)
		{
			continue;
		}

		const int32 BoneIndex = TargetMesh->GetBoneIndex(BodySetup->BoneName);
		FBodyInstance* BodyInstance = TargetMesh->GetBodyInstance(BodySetup->BoneName);
		if (BoneIndex == INDEX_NONE || !BodyInstance)
		{
			continue;
		}

		if (BodyInstance->IsInstanceSimulatingPhysics())
		{
			continue;
		}

		FTransform DesiredBodyTransform = TargetMesh->GetBoneTransform(BoneIndex);
		if (DeformationComponent)
		{
			const FVector DeformationOffsetWS = TargetMesh->GetComponentTransform().TransformVectorNoScale(
				DeformationComponent->GetBoneDeformationOffset(BodySetup->BoneName));
			DesiredBodyTransform.AddToTranslation(DeformationOffsetWS);
		}

		BodyInstance->SetInstanceSimulatePhysics(false);
		BodyInstance->SetBodyTransform(DesiredBodyTransform, ETeleportType::TeleportPhysics);
	}
}

FName ADeformationVehiclePawn::GetEffectiveSimulationRootBone() const
{
	if (!RootBone.IsNone())
	{
		return RootBone;
	}

	if (TargetMesh && TargetMesh->GetNumBones() > 0)
	{
		return TargetMesh->GetBoneName(0);
	}

	return NAME_None;
}
