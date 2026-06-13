# Deformation

Runtime Unreal Engine plugin for simple skeletal vehicle dent deformation.

## Easiest setup

Use `ADeformationVehiclePawn`.

1. Create a Blueprint from `DeformationVehiclePawn`.
2. Select `TargetMesh` and assign your vehicle Skeletal Mesh asset there.
3. Make sure the Skeletal Mesh has the Physics Asset you configured in PHAT.
4. Set `RootBone` to the chassis/root bone that must never move.
5. Place/move/attach the Blueprint anywhere; `TargetMesh` is the root component, so skeletal physics starts directly from the pawn/Blueprint transform instead of from a simulated child component.

The pawn already contains and configures:

- `TargetMesh` as the root component that owns the actor transform and skeletal physics.
- `PawnRoot` as a helper scene component for Blueprint organization.
- `TargetMesh` as the PHAT collision mesh: `RootBone` is simulated, while bodies below it are kinematic by default.
- `PoseableMesh` as a child visual copy whose bones are moved directly from C++.
- `DeformationComponent` bound to both meshes with direct deformation enabled.

By default you do not need Control Rig, an Animation Blueprint, or per-bone setup. Every hit bone can deform except the configured `RootBone`.

## Collision and physics defaults

`DeformationVehiclePawn` applies these defaults to `TargetMesh` in construction and at BeginPlay:

- `TargetMesh` is the actor root and is moved with `TeleportPhysics` before simulation is configured, which avoids Unreal's "Attempting to move a fully simulated skeletal mesh" warning and prevents start-up teleporting to world zero.
- `CollisionProfileName = PhysicsActor`.
- `CollisionEnabled = QueryAndPhysics`.
- `Simulation Generates Hit Events` is enabled with `SetNotifyRigidBodyCollision(true)` and `SetAllBodiesNotifyRigidBodyCollision(true)`.
- `Simulate Physics` and gravity are enabled by default on `TargetMesh`.
- `UseKinematicPhysicsBodies` is enabled by default: `RootBone` remains simulated, and child PHAT bodies are kinematic collision bodies that the deformation component moves inward on impacts. If `RootBone` is empty, the first skeleton bone is used as the simulated root. After switching bodies to kinematic, the pawn realigns every non-root body once to its current bone transform with teleport physics so they do not spawn at world zero.
- When `ForceBlockingPhysicsCollision` is enabled, the mesh object type is `PhysicsBody` and all channels block, so PHAT bodies are easy to see and test.
- All rigid bodies are woken at setup time.

`PoseableMesh` has collision disabled on purpose. It is only the visible deformed copy and is attached to `TargetMesh`, so it inherits the same root physics transform; use PHAT/debug collision on `TargetMesh`.

If you need a custom collision channel, change `CollisionProfileName` on the pawn, but keep it blocking the objects that should dent the vehicle.

## How deformation works

`UDeformationComponent` listens for `OnComponentHit` on `TargetMesh`, resolves the impacted PHAT body bone, converts collision normal impulse into an inward component-space offset, and moves the matching PHAT body inward. `PoseableMesh` copies the resulting `TargetMesh` pose every tick, so visual bones follow the moved PHAT bodies instead of receiving a second independent offset.

Kinematic PHAT hits can report zero `NormalImpulse`, so the component estimates an impulse from relative velocity via `KinematicHitImpulseScale`. That lets collision events from kinematic bodies still move bones and create visible dents. If Chaos reports the simulated `RootBone` for a hit while the actual dent bodies are kinematic, the component falls back to the closest non-root PHAT body to the hit point, so the chassis stays protected but doors/panels can still deform.

## Important settings

- `RootBone`: the root/chassis bone that must never receive deformation offsets or generated deformation impulses.
- `CollisionProfileName`: collision profile applied to `TargetMesh`; default is `PhysicsActor`.
- `ForceBlockingPhysicsCollision`: forces `TargetMesh` to `PhysicsBody` and blocks all channels for easier PHAT collision debugging.
- `SimulatePhysics`: enables skeletal physics on `TargetMesh`; enabled by default.
- `EnableGravity`: enables gravity on `TargetMesh`; enabled by default.
- `UseKinematicPhysicsBodies`: keeps bodies below `RootBone` kinematic while `RootBone` stays simulated; enabled by default. If `RootBone` is empty, the first skeleton bone is used. Non-root PHAT bodies are aligned back to their bone transforms during setup.
- `EstimateKinematicHitImpulse`: estimates dent strength from relative velocity when kinematic hits have zero impulse.
- Inward-only deformation: the hit normal is compared against the direction from impact point to mesh center, and flipped when needed so dents do not push outward.
- `OnlyConfiguredBones`: disabled by default, so bones deform automatically without filling a list. Enable it only if you want deformation limited to `BoneSettings`.
- `DefaultBoneSettings`: impulse thresholds and max dent offset used for automatically deforming bones.
- `BoneSettings`: optional per-bone overrides.
- `ApplyDirectBoneTransforms`: enabled by default to move the visible poseable bones directly from C++.
- `MovePhysicsBodyWithDeformation`: teleports the impacted PHAT body by the accepted inward dent delta while keeping that body kinematic. The poseable mesh also receives the stored offset directly, so the visual dent remains visible even when Chaos does not expose a kinematic body move as a skeletal pose change.
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
