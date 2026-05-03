#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CustomConvexMesh.h"
#include "CollisionBroadPhase.h"
#include "CustomCarPhysicsComponent.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;

USTRUCT()
struct FPhysicsState
{
    GENERATED_BODY()

    FVector Velocity = FVector::ZeroVector;
    FVector AngularVelocity = FVector::ZeroVector;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class DEFORMATION_API UCustomCarPhysicsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCustomCarPhysicsComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Custom Physics") float Mass = 1200.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Custom Physics") float FixedTimeStep = 1.0f / 60.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Custom Physics") FVector ProxyHalfExtents = FVector(120.0f, 60.0f, 40.0f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Custom Physics") float Restitution = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deformation") float DeformRadius = 50.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deformation") float MaxDeform = 8.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Optimization") float CollisionNeighborRadius = 2000.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Optimization") int32 MaxDeformationEventsPerFrame = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Optimization") int32 MaxDirtyVerticesPerFrame = 5000;

    UFUNCTION(BlueprintCallable, Category="Custom Physics")
    void ApplyImpact(FVector Point, FVector Normal, float Force);

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    float TimeAccumulator = 0.0f;
    FPhysicsState PhysicsState;

    FCustomConvexMesh ConvexMesh;
    TArray<FVector> CachedNormals;
    TSet<int32> DirtyVertices;
    TArray<FDeformationEvent> DeformationQueue;
    FCollisionBroadPhase BroadPhase;

    UPROPERTY() UStaticMeshComponent* VisualMesh = nullptr;
    UPROPERTY() UProceduralMeshComponent* RuntimeMesh = nullptr;

    void BuildRuntimeMeshFromStatic();
    void UpdateRuntimeMesh(bool bFullRebuildNormals);

    void SimulateFixedStep(float Dt);
    void IntegrateMovement(float Dt);
    void HandleWorldCollision();
    void HandleCarCollisions();
    void ProcessDeformationQueue();
};
