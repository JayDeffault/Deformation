# Deformation

Runtime Unreal Engine plugin for simple skeletal vehicle dent deformation.

## Easiest setup

Use `ADeformationVehiclePawn`.

1. Create a Blueprint from `DeformationVehiclePawn`.
2. Select the root `TargetMesh` component and assign your vehicle Skeletal Mesh asset there.
3. Make sure the Skeletal Mesh has the Physics Asset you configured in PHAT.
4. Set `RootBone` to the chassis/root bone that must never move.
5. Place the Blueprint anywhere in the level and play.

The pawn already contains and configures:

- `TargetMesh` as the RootComponent for collision, PHAT physics bodies, hit events, and physics simulation.
- `PoseableMesh` as a child visual copy whose bones are moved directly from C++.
- `DeformationComponent` bound to both meshes with direct deformation enabled.

By default you do not need Control Rig, an Animation Blueprint, or per-bone setup. Every hit bone can deform except the configured `RootBone`.

## Collision and physics defaults

`DeformationVehiclePawn` applies these defaults to `TargetMesh` in construction and at BeginPlay:

- `CollisionProfileName = PhysicsActor`.
- `CollisionEnabled = QueryAndPhysics`.
- `Simulation Generates Hit Events` is enabled with `SetNotifyRigidBodyCollision(true)`.
- `Simulate Physics` is enabled by default.
- All rigid bodies are woken at setup time.

`PoseableMesh` has collision disabled on purpose. It is only the visible deformed copy. Use PHAT/debug collision on `TargetMesh`; the plugin hides `TargetMesh` in game instead of disabling its editor visibility, so physics/collision debug remains available while playing.

If you need a custom collision channel, change `CollisionProfileName` on the pawn, but keep it blocking the objects that should dent the vehicle.

## How deformation works

`UDeformationComponent` listens for `OnComponentHit` on `TargetMesh`, resolves the impacted physics body bone, converts collision normal impulse into an inward component-space offset, and writes that offset to the same bone on `PoseableMesh` with `SetBoneLocationByName`.

`TargetMesh` owns collision and physics. `PoseableMesh` follows `TargetMesh` at relative transform identity, copies its mesh/materials, and renders the deformed result at the actor's placed transform rather than at world zero.

## Important settings

- `RootBone`: the root/chassis bone that must never receive deformation offsets or generated deformation impulses.
- `CollisionProfileName`: collision profile applied to `TargetMesh`; default is `PhysicsActor`.
- `SimulatePhysics`: enables physics simulation on `TargetMesh`; requires a valid Physics Asset.
- `OnlyConfiguredBones`: disabled by default, so bones deform automatically without filling a list. Enable it only if you want deformation limited to `BoneSettings`.
- `DefaultBoneSettings`: impulse thresholds and max dent offset used for automatically deforming bones.
- `BoneSettings`: optional per-bone overrides.
- `ApplyDirectBoneTransforms`: enabled by default to move the visible poseable bones directly from C++.
- `ApplyPhysicsImpulse`: also pushes the impacted physics body inward when a valid deformation hit is accepted.
- `RecoverySpeed`: keep `0` for permanent dents, or set above `0` for dents that return toward zero.

## Runtime API

- `ConfigureDeformation`: reapplies pawn collision/physics/deformation wiring after changing settings.
- `ApplyDeformationImpulse`: manually deform a bone from traces, damage events, or custom collision code.
- `IsRootBone`: check if a bone is the protected root bone.
- `RefreshDirectBoneTransforms`: reapplies all stored offsets to the poseable mesh.
- `InitializeDirectBoneTransforms`: creates/configures the poseable visual mesh when using the component outside the pawn.
- `GetBoneDeformationOffset`, `GetAllDeformationStates`, `ResetDeformation`: inspect or clear deformation state.
- `OnBoneDeformed`: Blueprint event fired when a bone receives a valid deformation impulse.
