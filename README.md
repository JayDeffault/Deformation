# Deformation

Runtime Unreal Engine plugin for skeletal vehicle collision deformation.

## What it does

`UDeformationComponent` binds to a `USkeletalMeshComponent`, listens for `OnComponentHit`, resolves the hit physics body bone, converts the collision normal impulse into an inward component-space offset, and stores that offset per bone. It can also apply an immediate physics impulse to the matching skeletal body so simulated deformation bones react at impact time.

## Basic setup

1. Enable physics collision notifications on the vehicle skeletal mesh. The component also calls `SetNotifyRigidBodyCollision(true)` when it binds, but collision presets must still block the impact channel.
2. Add `DeformationComponent` to the vehicle actor.
3. Assign `TargetMesh`, or leave it empty to use the owner's first skeletal mesh component.
4. Fill `BoneSettings` with deformable physics body bone names, for example `door_l_deform_01`, `hood_deform_02`, or `trunk_deform_01`.
5. In an Animation Blueprint or Control Rig, query `GetBoneDeformationOffset(BoneName)` and use it as a translation offset for the corresponding deform bone.
6. If deformation bones are simulated physics bodies, keep `Apply Physics Impulse` enabled so the body is kicked inward on impact.

## Important settings

- `MinImpulse`: filters weak touches.
- `ImpulseForMaxOffset`: impulse value that maps to `MaxOffset`.
- `MaxOffset`: clamp for accumulated dent translation in centimeters.
- `PhysicsImpulseScale`: scales the impulse sent to the physics body.
- `RecoverySpeed`: set to `0` for permanent dents, or above `0` for spring-like recovery.
- `OnlyConfiguredBones`: when enabled, only bones listed in `BoneSettings` can deform.

## Runtime API

- `ApplyDeformationImpulse`: manually deform a bone from traces, damage events, or custom collision code.
- `GetBoneDeformationOffset`: read an offset for Animation Blueprint / Control Rig use.
- `GetAllDeformationStates`: read all active dents for UI, saving, or debugging.
- `ResetDeformation`: clear one bone or all deformation state.
- `OnBoneDeformed`: Blueprint event fired whenever a bone receives a valid deformation impulse.
