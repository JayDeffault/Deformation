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
- `PoseableMesh` as a child visual copy whose bones are moved directly from C++ and casts the visible shadow.
- `DeformationComponent` bound to both meshes with direct deformation enabled.

By default you do not need Control Rig, an Animation Blueprint, or per-bone setup. Every hit bone can deform except the configured `RootBone`.

## Collision and physics defaults

`DeformationVehiclePawn` applies these defaults to `TargetMesh` in construction and at BeginPlay:

- `TargetMesh` is the actor root and is moved with `TeleportPhysics` before simulation is configured, which avoids Unreal's "Attempting to move a fully simulated skeletal mesh" warning and prevents start-up teleporting to world zero.
- `CollisionProfileName = PhysicsActor`.
- `CollisionEnabled = QueryAndPhysics`.
- `Simulation Generates Hit Events` is enabled with `SetNotifyRigidBodyCollision(true)` and `SetAllBodiesNotifyRigidBodyCollision(true)`.
- `Simulate Physics` and gravity are enabled by default on `TargetMesh`.
- `UseKinematicPhysicsBodies` is enabled by default, but the pawn no longer rewrites PHAT simulation modes. Configure `RootBone`, simulated doors/hood/trunk, constraints, and kinematic deformation bodies in PHAT; the pawn respects those settings and only aligns non-simulated non-root bodies during setup.
- When `ForceBlockingPhysicsCollision` is enabled, the mesh object type is `PhysicsBody` and all channels block, so PHAT bodies are easy to see and test.
- All rigid bodies are woken at setup time.

`PoseableMesh` has collision disabled on purpose. It is the only visible deformed copy and is attached to `TargetMesh`, so it inherits the same root physics transform; use PHAT/debug collision on `TargetMesh`. When the target render mesh is hidden, it stays visible to debug collision tools but is removed from the main render pass and shadow rendering, so the hidden physics mesh does not draw a second set of visible polygons while the child `PoseableMesh` remains visible.

If you need a custom collision channel, change `CollisionProfileName` on the pawn, but keep it blocking the objects that should dent the vehicle.

## How deformation works

`UDeformationComponent` listens for `OnComponentHit` on `TargetMesh`, resolves the impacted PHAT body bone, converts collision normal impulse into an inward component-space offset, and moves the matching PHAT body inward. `PoseableMesh` copies the resulting `TargetMesh` pose every tick and then applies current PHAT body transforms, so simulated doors/hinges are visible even when the hidden physics mesh is not rendered.

Kinematic PHAT hits can report zero `NormalImpulse`, so the component estimates an impulse from velocity along the hit normal via `KinematicHitImpulseScale`; tangential rubbing/sliding is ignored. That lets collision events from kinematic bodies still move bones and create visible dents. If Chaos reports a simulated `RootBone`, door, hinge, or chassis body while the actual dent bodies are kinematic, the component prefers the closest kinematic non-root PHAT body to the hit point, so debug collision bodies for dents move together with the visual deformation.

By default, accepted hits deform along each bone normal axis, accumulate inward dent depth up to `MaxOffset`, and opposite hits cannot push the dent back out. This is intended for vehicles whose deformation bones are authored like a hedgehog: set `BoneNormalAxis` to the local axis that points inward for those bones.

Every resolved hit bone can deform by default again, including simulated bodies, matching the original simple dent behavior. If you need to protect simulated constraint bodies such as doors/hood/trunk hinges, enable `DeformOnlyKinematicBodies` and use separate kinematic deformation bodies on those parts.

If a kinematic deformation body is parented under a simulated body (for example a dent helper under a simulated door), the pawn keeps that kinematic body attached to the nearest simulated parent body each tick before applying the stored dent offset. This lets the door open while its kinematic deformation bodies follow it.

## Important settings

- `RootBone`: the root/chassis bone that must never receive deformation offsets or generated deformation impulses.
- `ForceInwardDeformation`: exposed on `ADeformationVehiclePawn` and forwarded to `UDeformationComponent`; it is enabled by default and can be disabled in the pawn Details panel if you need raw hit-normal deformation.
- `CollisionProfileName`: collision profile applied to `TargetMesh`; default is `PhysicsActor`.
- `ForceBlockingPhysicsCollision`: forces `TargetMesh` to `PhysicsBody` and blocks all channels for easier PHAT collision debugging.
- `SimulatePhysics`: enables skeletal physics on `TargetMesh`; enabled by default.
- `EnableGravity`: enables gravity on `TargetMesh`; enabled by default.
- `UseKinematicPhysicsBodies`: respects PHAT simulation settings; simulated bodies remain simulated for constraints, while kinematic bodies are used as deformation helpers. Non-simulated non-root PHAT bodies are aligned back to their bone transforms during setup.
- `EstimateKinematicHitImpulse`: estimates dent strength from relative velocity when kinematic hits have zero impulse.
- `ForceInwardDeformation`: enabled by default as a fallback when bone-normal deformation is disabled.
- `UseBoneNormalForDeformation`: enabled by default; dents move along the selected local `BoneNormalAxis` instead of arbitrary collision normals.
- `BoneNormalAxis`: default is `NegativeX`; change it if your hedgehog deformation bones point inward on another local axis.
- `AccumulateHitsToMaxOffset`: enabled by default; each accepted hit adds dent depth until `MaxOffset`, and opposite hits cannot push the dent back out.
- `OnlyConfiguredBones`: disabled by default, so bones deform automatically without filling a list. Enable it only if you want deformation limited to `BoneSettings`.
- `DefaultBoneSettings`: impulse thresholds and max dent offset used for automatically deforming bones.
- `BoneSettings`: optional per-bone overrides.
- `bLockDeformationDirection`: optional per-bone one-way mode.
- `bAccumulateHitsToMaxOffset`: global one-way accumulation mode; enabled by default to deepen dents up to the configured maximum without allowing push-back.
- `bUseCustomDeformationDirection` / `DeformationDirectionCS`: optional per-bone component-space dent direction when directional locking is needed.
- `ApplyDirectBoneTransforms`: enabled by default to move the visible poseable bones directly from C++.
- `MovePhysicsBodyWithDeformation`: enabled on the pawn by default; teleports the impacted PHAT body by the accepted dent delta so debug collision bodies move together with the visible deformation.
- `SyncPhysicsBodiesToPoseableBones`: enabled on the pawn by default; after final poseable bone offsets are applied, matching PHAT body locations are refreshed from those deformed bone locations so collision/debug bodies stay attached to the visual dents.
- `PreferKinematicBodiesForDeformation`: enabled on the pawn by default; when a simulated door/chassis body reports the hit, deformation is redirected to the nearest kinematic helper body so the moved PHAT body matches the visible dent. The poseable mesh also receives the stored offset directly, so the visual dent remains visible even when Chaos does not expose a body move as a skeletal pose change.
- `DeformOnlyKinematicBodies`: disabled by default; enable it only when simulated constrained bodies must never receive deformation offsets.
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
