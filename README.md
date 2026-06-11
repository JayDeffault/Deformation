# Deformation

Runtime Unreal Engine plugin for simple skeletal vehicle dent deformation.

## Easiest setup

Use `ADeformationVehiclePawn`.

1. Create a Blueprint from `DeformationVehiclePawn`.
2. Select the root `TargetMesh` component and assign your vehicle Skeletal Mesh asset there.
3. Set `RootBone` to the chassis/root bone that must never move.
4. Play. The pawn already contains:
   - `TargetMesh` as the RootComponent for collision, physics bodies, and hit events.
   - `PoseableMesh` as the visible copy whose bones are moved directly from C++.
   - `DeformationComponent` bound to both meshes with direct deformation enabled.

By default you do not need Control Rig, an Animation Blueprint, or per-bone setup. Every hit bone can deform except the configured `RootBone`.

## How it works

`UDeformationComponent` listens for `OnComponentHit` on `TargetMesh`, resolves the impacted physics body bone, converts collision normal impulse into an inward component-space offset, and writes that offset to the same bone on `PoseableMesh` with `SetBoneLocationByName`.

`TargetMesh` stays hidden visually when the poseable copy is active, but it still owns collision and physics. `PoseableMesh` has collision disabled and is only the visible deformed mesh.

## Important settings

- `RootBone`: the root/chassis bone that must never receive deformation offsets or generated deformation impulses.
- `OnlyConfiguredBones`: disabled by default, so bones deform automatically without filling a list. Enable it only if you want deformation limited to `BoneSettings`.
- `DefaultBoneSettings`: impulse thresholds and max dent offset used for automatically deforming bones.
- `BoneSettings`: optional per-bone overrides.
- `ApplyDirectBoneTransforms`: enabled by default to move the visible poseable bones directly from C++.
- `ApplyPhysicsImpulse`: also pushes the impacted physics body inward when a valid deformation hit is accepted.
- `RecoverySpeed`: keep `0` for permanent dents, or set above `0` for dents that return toward zero.

## Runtime API

- `ApplyDeformationImpulse`: manually deform a bone from traces, damage events, or custom collision code.
- `IsRootBone`: check if a bone is the protected root bone.
- `RefreshDirectBoneTransforms`: reapplies all stored offsets to the poseable mesh.
- `InitializeDirectBoneTransforms`: creates/configures the poseable visual mesh when using the component outside the pawn.
- `GetBoneDeformationOffset`, `GetAllDeformationStates`, `ResetDeformation`: inspect or clear deformation state.
- `OnBoneDeformed`: Blueprint event fired when a bone receives a valid deformation impulse.
