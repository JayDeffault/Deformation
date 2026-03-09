#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "CarMeshDeformationComponent.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UStaticMesh;
class FCarRHIDentUploader;
struct FHitResult;

USTRUCT(BlueprintType)
struct FRuntimeDent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	FVector LocalCenter = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	FVector LocalNormal = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	float Radius = 35.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	float CurrentDepth = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	float TargetDepth = 5.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deformation")
	float RemainingTime = 6.0f;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class DEFORMATION_API UCarMeshDeformationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCarMeshDeformationComponent();
	virtual ~UCarMeshDeformationComponent() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Deformation")
	void AddDentWorld(const FVector& WorldPoint, const FVector& WorldNormal, float Strength);

protected:
	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	void CacheProxyState();
	void UpdateCollisionProxiesBudgeted(float DeltaTime);
	FVector EvaluateDentOffset(const FVector& LocalPoint) const;
	void TickDentSmoothing(float DeltaTime);
	void MergeOrAddDent(const FRuntimeDent& InDent);
	void UploadDentsToRHI();
	void InitializeProceduralVisualMesh();
	void UpdateProceduralVisualMesh(float DeltaTime);
	void InitializeDeformableCollisionMesh();
	void UpdateDeformableCollisionMesh(float DeltaTime);
	void InitializeLowLevelConvexCollision();
	void UpdateLowLevelConvexCollision();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation")
	TObjectPtr<UStaticMeshComponent> VisualMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Collision")
	TObjectPtr<UStaticMesh> DeformableCollisionStaticMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation")
	TArray<TObjectPtr<UBoxComponent>> ConvexBoxes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation", meta=(ClampMin="1", ClampMax="128"))
	int32 MaxRuntimeDents = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation")
	float MaxDentDepth = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation")
	float DentRadius = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation")
	float HitToDepthScale = 0.0020f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation", meta=(ClampMin="0.0"))
	float MinImpactForDent = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation", meta=(ClampMin="0.01", ClampMax="0.5"))
	float HitCooldown = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation")
	float DentLifetime = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation", meta=(ClampMin="1.0", ClampMax="40.0"))
	float DentSmoothSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation", meta=(ClampMin="0.0", ClampMax="50.0"))
	float DentMergeDistance = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Performance")
	bool bEnableCollisionProxyUpdate = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Performance", meta=(ClampMin="0.01", ClampMax="0.5", EditCondition="bEnableCollisionProxyUpdate"))
	float CollisionUpdateInterval = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Performance", meta=(ClampMin="1", ClampMax="8", EditCondition="bEnableCollisionProxyUpdate"))
	int32 MaxProxyUpdatesPerTick = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Performance", meta=(EditCondition="bEnableCollisionProxyUpdate"))
	bool bUseLowLevelCollisionVertexPath = true;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Visual")
	bool bEnableProceduralVisualDeformation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Visual", meta=(EditCondition="bEnableProceduralVisualDeformation", ClampMin="0.01", ClampMax="0.2"))
	float VisualMeshUpdateInterval = 0.033f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Collision", meta=(EditCondition="bEnableProceduralVisualDeformation"))
	bool bDeformProceduralCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Collision", meta=(EditCondition="bDeformProceduralCollision", ClampMin="0.01", ClampMax="0.2"))
	float CollisionSyncInterval = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Collision", meta=(EditCondition="bDeformProceduralCollision"))
	bool bUseLowLevelConvexCollision = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|RHI")
	bool bUseRHIDeformationPipeline = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|RHI", meta=(EditCondition="bUseRHIDeformationPipeline", ClampMin="8", ClampMax="256"))
	int32 RHIMaxDents = 64;

private:
	struct FBoxProxyCache
	{
		TObjectPtr<UBoxComponent> Box = nullptr;
		FTransform InitialRelativeTransform = FTransform::Identity;
		FVector InitialExtent = FVector(10.0f);
		TArray<FVector> InitialCornersLocal;
		TArray<FVector3f> InitialCornersMeshLocalF;
		TArray<FVector3f> DeformedCornersMeshLocalF;
	};


	void EvaluateProxyVerticesLowLevel(FBoxProxyCache& Cached, FVector& OutMin, FVector& OutMax) const;

	UPROPERTY(Transient)
	TArray<FRuntimeDent> RuntimeDents;

	TArray<FBoxProxyCache> CachedBoxes;

	int32 NextProxyIndex = 0;
	float CollisionUpdateTimer = 0.0f;
	float VisualUpdateTimer = 0.0f;
	float CollisionSyncTimer = 0.0f;
	float LastAcceptedHitTime = -1000.0f;

	TObjectPtr<UProceduralMeshComponent> ProceduralVisualMesh = nullptr;
	TArray<FVector> BaseVisualVertices;
	TArray<FVector> DeformedVisualVertices;
	TArray<int32> VisualTriangles;
	TArray<FVector> VisualNormals;
	TArray<FVector2D> VisualUV0;
	TArray<FColor> VisualColors;
	TArray<FProcMeshTangent> VisualTangents;

	TObjectPtr<UProceduralMeshComponent> DeformableCollisionMesh = nullptr;
	TArray<FVector> BaseCollisionVertices;
	TArray<FVector> DeformedCollisionVertices;
	TArray<int32> CollisionTriangles;
	TArray<FVector> CollisionNormals;
	TArray<FVector2D> CollisionUV0;
	TArray<FColor> CollisionColors;
	TArray<FProcMeshTangent> CollisionTangents;
	TArray<FKConvexElem> BaseLowLevelConvexElems;
	TArray<FKConvexElem> DeformedLowLevelConvexElems;

	TSharedPtr<FCarRHIDentUploader> RHIDentUploader;
};
