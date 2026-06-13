# Deformation

Runtime Unreal Engine plugin for simple skeletal vehicle dent deformation.

## Easiest setup

Use `ADeformationVehiclePawn`.

1. Create a Blueprint from `DeformationVehiclePawn`.
2. Select `TargetMesh` and assign your vehicle Skeletal Mesh asset there.
3. Make sure the Skeletal Mesh has the Physics Asset you configured in PHAT.
4. Set `RootBone` to the chassis/root bone that must never move.
5. Place/move/attach the Blueprint anywhere; `PawnRoot`, `TargetMesh`, and `PoseableMesh` all share relative identity, so they follow the pawn transform.

The pawn already contains and configures:

- `PawnRoot` as the root scene component that owns the actor transform.
- `TargetMesh` as the PHAT collision mesh with `Simulate Physics` and gravity enabled by default.
- `PoseableMesh` as a child visual copy whose bones are moved directly from C++.
- `DeformationComponent` bound to both meshes with direct deformation enabled.

By default you do not need Control Rig, an Animation Blueprint, or per-bone setup. Every hit bone can deform except the configured `RootBone`.

## Collision and physics defaults

`DeformationVehiclePawn` applies these defaults to `TargetMesh` in construction and at BeginPlay:

- Before physics simulation starts, `TargetMesh` and `PoseableMesh` are attached under `PawnRoot` and keep relative transform identity. When full physics simulation is enabled, `TargetMesh` is driven by Chaos and `PoseableMesh` follows `TargetMesh`.
- `CollisionProfileName = PhysicsActor`.
- `CollisionEnabled = QueryAndPhysics`.
- `Simulation Generates Hit Events` is enabled with `SetNotifyRigidBodyCollision(true)` and `SetAllBodiesNotifyRigidBodyCollision(true)`.
- `Simulate Physics` and gravity are enabled by default, so the Skeletal Mesh should fall/react physically instead of hanging in the air.
- When `ForceBlockingPhysicsCollision` is enabled, the mesh object type is `PhysicsBody` and all channels block, so PHAT bodies are easy to see and test.
- `UseKinematicPhysicsBodies` is available if you want PHAT bodies to stay attached to the pawn transform instead of full Chaos simulation.
- All rigid bodies are woken at setup time.

`PoseableMesh` has collision disabled on purpose. It is only the visible deformed copy. During full physics simulation it follows `TargetMesh`; use PHAT/debug collision on `TargetMesh`.

If you need a custom collision channel, change `CollisionProfileName` on the pawn, but keep it blocking the objects that should dent the vehicle.

## How deformation works

`UDeformationComponent` listens for `OnComponentHit` on `TargetMesh`, resolves the impacted PHAT body bone, converts collision normal impulse into an inward component-space offset, and writes that offset to the same bone on `PoseableMesh` with `SetBoneLocationByName`.

Kinematic PHAT hits can report zero `NormalImpulse`, so the component estimates an impulse from relative velocity via `KinematicHitImpulseScale`. That lets collision events from kinematic bodies still move bones and create visible dents.

## Important settings

- `RootBone`: the root/chassis bone that must never receive deformation offsets or generated deformation impulses.
- `CollisionProfileName`: collision profile applied to `TargetMesh`; default is `PhysicsActor`.
- `ForceBlockingPhysicsCollision`: forces `TargetMesh` to `PhysicsBody` and blocks all channels for easier PHAT collision debugging.
- `SimulatePhysics`: enables full skeletal physics simulation; enabled by default.
- `EnableGravity`: enables gravity on `TargetMesh`; enabled by default.
- `UseKinematicPhysicsBodies`: disables full simulation and keeps PHAT bodies attached to the pawn transform for kinematic collision tests.
- `EstimateKinematicHitImpulse`: estimates dent strength from relative velocity when kinematic hits have zero impulse.
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
