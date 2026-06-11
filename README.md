# Deformation

Runtime Unreal Engine plugin for skeletal vehicle collision deformation.

## What it does

`UDeformationComponent` binds to a `USkeletalMeshComponent`, listens for `OnComponentHit`, resolves the hit physics body bone, converts the collision normal impulse into an inward component-space offset, and stores that offset per bone. It can also apply an immediate physics impulse to the matching skeletal body so simulated deformation bones react at impact time.

## Direct C++ bone movement, without Control Rig or Animation Blueprint

Yes, this plugin can work without Control Rig and without an Animation Blueprint. Enable `Apply Direct Bone Transforms` on `DeformationComponent`. The plugin does not try to overwrite the internal pose of `USkeletalMeshComponent` directly, because regular animation/physics evaluation can replace those transforms; instead it renders a poseable copy whose bones are controlled entirely from C++.

In that mode the component uses a `UPoseableMeshComponent` as the visible mesh and writes bone locations directly from C++ with `SetBoneLocationByName`:

1. `TargetMesh` stays on the vehicle and remains responsible for collision, hit events, physics bodies, and impulses.
2. `PoseableMesh` is the visual copy whose bones are moved directly by code.
3. If `PoseableMesh` is not assigned and `Auto Create Poseable Mesh` is enabled, the component creates it at runtime from `TargetMesh`.
4. If `Hide Target Mesh When Using Poseable` is enabled, the original skeletal mesh is hidden visually but still keeps collision/physics active.

This route is best for first-stage dent/deformation bones. If you later need complex animation blending, wheel/door animation layers, or network-perfect animation state, you can still disable direct transforms and consume `GetBoneDeformationOffset` in an animation graph.

## Basic setup

1. Enable physics collision notifications on the vehicle skeletal mesh. The component also calls `SetNotifyRigidBodyCollision(true)` when it binds, but collision presets must still block the impact channel.
2. Add `DeformationComponent` to the vehicle actor.
3. Assign `TargetMesh`, or leave it empty to use the owner's first skeletal mesh component.
4. Leave `Apply Direct Bone Transforms` enabled to avoid Control Rig / Anim Blueprint.
5. Fill `BoneSettings` with deformable physics body bone names, for example `door_l_deform_01`, `hood_deform_02`, or `trunk_deform_01`.
6. If deformation bones are simulated physics bodies, keep `Apply Physics Impulse` enabled so the body is kicked inward on impact.

## Important settings

- `ApplyDirectBoneTransforms`: writes the deformation directly into `PoseableMesh` from C++.
- `PoseableMesh`: optional pre-made poseable visual mesh; if empty, the plugin can create one.
- `AutoCreatePoseableMesh`: creates a runtime visual clone from `TargetMesh`.
- `HideTargetMeshWhenUsingPoseable`: hides the physics/collision skeletal mesh while the poseable clone renders.
- `MinImpulse`: filters weak touches.
- `ImpulseForMaxOffset`: impulse value that maps to `MaxOffset`.
- `MaxOffset`: clamp for accumulated dent translation in centimeters.
- `PhysicsImpulseScale`: scales the impulse sent to the physics body.
- `RecoverySpeed`: set to `0` for permanent dents, or above `0` for spring-like recovery.
- `OnlyConfiguredBones`: when enabled, only bones listed in `BoneSettings` can deform.

## Runtime API

- `ApplyDeformationImpulse`: manually deform a bone from traces, damage events, or custom collision code.
- `RefreshDirectBoneTransforms`: reapplies all stored offsets to the poseable mesh.
- `InitializeDirectBoneTransforms`: creates/configures the poseable visual mesh.
- `SetPoseableMesh`: assigns a custom poseable visual mesh.
- `GetBoneDeformationOffset`: read an offset if you choose to drive another system manually.
- `GetAllDeformationStates`: read all active dents for UI, saving, or debugging.
- `ResetDeformation`: clear one bone or all deformation state.
- `OnBoneDeformed`: Blueprint event fired whenever a bone receives a valid deformation impulse.
