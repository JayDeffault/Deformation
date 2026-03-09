#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CarMeshDeformationComponent.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation")
	TObjectPtr<UStaticMeshComponent> VisualMesh = nullptr;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation")
	float DentLifetime = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation", meta=(ClampMin="1.0", ClampMax="40.0"))
	float DentSmoothSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation", meta=(ClampMin="0.0", ClampMax="50.0"))
	float DentMergeDistance = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Performance")
	bool bEnableCollisionProxyUpdate = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Performance", meta=(ClampMin="0.01", ClampMax="0.5", EditCondition="bEnableCollisionProxyUpdate"))
	float CollisionUpdateInterval = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Performance", meta=(ClampMin="1", ClampMax="8", EditCondition="bEnableCollisionProxyUpdate"))
	int32 MaxProxyUpdatesPerTick = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|Performance", meta=(EditCondition="bEnableCollisionProxyUpdate"))
	bool bUseLowLevelCollisionVertexPath = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deformation|RHI")
	bool bUseRHIDeformationPipeline = true;

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

	TUniquePtr<FCarRHIDentUploader> RHIDentUploader;
};
