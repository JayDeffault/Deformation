#include "CarMeshDeformationComponent.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "RHI.h"
#include "RHICommandList.h"

struct FRHIDentPayload
{
	FVector3f LocalCenter = FVector3f::ZeroVector;
	FVector3f LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
	float Radius = 0.0f;
	float Depth = 0.0f;
};

class FCarRHIDentUploader
{
public:
	~FCarRHIDentUploader()
	{
		Release();
	}

	void Initialize(uint32 MaxDentCount)
	{
		if (!IsInGameThread())
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(InitDentBuffer)(
			[this, MaxDentCount](FRHICommandListImmediate& RHICmdList)
			{
				const uint32 Bytes = FMath::Max(1u, MaxDentCount) * static_cast<uint32>(sizeof(FRHIDentPayload));
				FRHIResourceCreateInfo CreateInfo(TEXT("CarDentPayloadBuffer"));
				DentBuffer = RHICmdList.CreateVertexBuffer(Bytes, static_cast<uint32>(BUF_Dynamic | BUF_ShaderResource), CreateInfo);
				Capacity = MaxDentCount;
			});
	}

	void Upload(const TArray<FRuntimeDent>& RuntimeDents, uint32 MaxDentCount)
	{
		if (!IsInGameThread())
		{
			return;
		}

		if (!DentBuffer.IsValid() || Capacity != MaxDentCount)
		{
			Initialize(MaxDentCount);
		}

		TArray<FRHIDentPayload> Payload;
		Payload.Reserve(MaxDentCount);

		const int32 CountToCopy = FMath::Min(static_cast<int32>(MaxDentCount), RuntimeDents.Num());
		for (int32 Index = 0; Index < CountToCopy; ++Index)
		{
			const FRuntimeDent& Dent = RuntimeDents[Index];
			if (Dent.CurrentDepth <= 0.01f)
			{
				continue;
			}

			FRHIDentPayload Item;
			Item.LocalCenter = FVector3f(Dent.LocalCenter);
			Item.LocalNormal = FVector3f(Dent.LocalNormal);
			Item.Radius = Dent.Radius;
			Item.Depth = Dent.CurrentDepth;
			Payload.Add(Item);
		}

		Payload.SetNum(MaxDentCount);

		ENQUEUE_RENDER_COMMAND(UploadDentBuffer)(
			[this, Payload = MoveTemp(Payload)](FRHICommandListImmediate& RHICmdList)
			{
				if (!DentBuffer.IsValid())
				{
					return;
				}

				const uint32 NumBytes = Payload.Num() * sizeof(FRHIDentPayload);
				void* Data = RHICmdList.LockBuffer(DentBuffer, 0, NumBytes, RLM_WriteOnly);
				FMemory::Memcpy(Data, Payload.GetData(), NumBytes);
				RHICmdList.UnlockBuffer(DentBuffer);
			});
	}

	void Release()
	{
		if (!IsInGameThread())
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(ReleaseDentBuffer)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				DentBuffer.SafeRelease();
				Capacity = 0;
			});
	}

private:
	FBufferRHIRef DentBuffer;
	uint32 Capacity = 0;
};

namespace
{
	static TArray<FVector> BuildCorners(const FVector& Extent)
	{
		TArray<FVector> Corners;
		Corners.Reserve(8);

		for (int32 XSign = -1; XSign <= 1; XSign += 2)
		{
			for (int32 YSign = -1; YSign <= 1; YSign += 2)
			{
				for (int32 ZSign = -1; ZSign <= 1; ZSign += 2)
				{
					Corners.Add(FVector(Extent.X * XSign, Extent.Y * YSign, Extent.Z * ZSign));
				}
			}
		}

		return Corners;
	}
}

UCarMeshDeformationComponent::UCarMeshDeformationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UCarMeshDeformationComponent::~UCarMeshDeformationComponent() = default;

void UCarMeshDeformationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!VisualMesh)
	{
		VisualMesh = GetOwner() ? GetOwner()->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	}

	if (VisualMesh)
	{
		VisualMesh->OnComponentHit.AddDynamic(this, &UCarMeshDeformationComponent::OnMeshHit);
	}

	CacheProxyState();

	if (bUseRHIDeformationPipeline)
	{
		RHIDentUploader = MakeShared<FCarRHIDentUploader>();
		RHIDentUploader->Initialize(static_cast<uint32>(FMath::Max(8, RHIMaxDents)));
	}
}

void UCarMeshDeformationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RHIDentUploader.IsValid())
	{
		RHIDentUploader->Release();
		RHIDentUploader.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void UCarMeshDeformationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TickDentSmoothing(DeltaTime);
	UploadDentsToRHI();

	if (bEnableCollisionProxyUpdate)
	{
		UpdateCollisionProxiesBudgeted(DeltaTime);
	}
}

void UCarMeshDeformationComponent::UploadDentsToRHI()
{
	if (!bUseRHIDeformationPipeline || !RHIDentUploader.IsValid())
	{
		return;
	}

	const uint32 MaxUpload = static_cast<uint32>(FMath::Max(8, RHIMaxDents));
	RHIDentUploader->Upload(RuntimeDents, MaxUpload);
}

void UCarMeshDeformationComponent::TickDentSmoothing(float DeltaTime)
{
	for (int32 i = RuntimeDents.Num() - 1; i >= 0; --i)
	{
		FRuntimeDent& Dent = RuntimeDents[i];
		Dent.RemainingTime -= DeltaTime;
		Dent.CurrentDepth = FMath::FInterpTo(Dent.CurrentDepth, Dent.TargetDepth, DeltaTime, DentSmoothSpeed);

		if (Dent.RemainingTime <= 0.0f)
		{
			Dent.TargetDepth = 0.0f;
		}

		if (Dent.RemainingTime <= -0.8f && Dent.CurrentDepth <= 0.1f)
		{
			RuntimeDents.RemoveAtSwap(i);
		}
	}
}

void UCarMeshDeformationComponent::MergeOrAddDent(const FRuntimeDent& InDent)
{
	for (FRuntimeDent& Existing : RuntimeDents)
	{
		if (FVector::DistSquared(Existing.LocalCenter, InDent.LocalCenter) <= FMath::Square(DentMergeDistance))
		{
			Existing.TargetDepth = FMath::Clamp(FMath::Max(Existing.TargetDepth, InDent.TargetDepth), 0.0f, MaxDentDepth);
			Existing.LocalNormal = (Existing.LocalNormal + InDent.LocalNormal).GetSafeNormal();
			Existing.RemainingTime = FMath::Max(Existing.RemainingTime, InDent.RemainingTime);
			return;
		}
	}

	if (RuntimeDents.Num() >= MaxRuntimeDents)
	{
		int32 ReplaceIndex = 0;
		float MinScore = TNumericLimits<float>::Max();
		for (int32 i = 0; i < RuntimeDents.Num(); ++i)
		{
			const float Score = RuntimeDents[i].TargetDepth + FMath::Max(RuntimeDents[i].RemainingTime, 0.0f);
			if (Score < MinScore)
			{
				MinScore = Score;
				ReplaceIndex = i;
			}
		}
		RuntimeDents[ReplaceIndex] = InDent;
		return;
	}

	RuntimeDents.Add(InDent);
}

void UCarMeshDeformationComponent::AddDentWorld(const FVector& WorldPoint, const FVector& WorldNormal, float Strength)
{
	if (!VisualMesh)
	{
		return;
	}

	FRuntimeDent Dent;
	Dent.LocalCenter = VisualMesh->GetComponentTransform().InverseTransformPosition(WorldPoint);
	Dent.LocalNormal = VisualMesh->GetComponentTransform().InverseTransformVectorNoScale(WorldNormal).GetSafeNormal();
	Dent.TargetDepth = FMath::Clamp(Strength, 1.0f, MaxDentDepth);
	Dent.CurrentDepth = FMath::Max(0.2f, Dent.TargetDepth * 0.2f);
	Dent.Radius = DentRadius;
	Dent.RemainingTime = DentLifetime;
	MergeOrAddDent(Dent);
}

void UCarMeshDeformationComponent::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	const float ImpulseMagnitude = NormalImpulse.Size();
	const float Depth = FMath::Clamp(ImpulseMagnitude * HitToDepthScale, 1.0f, MaxDentDepth);
	AddDentWorld(Hit.ImpactPoint, Hit.ImpactNormal, Depth);
}

void UCarMeshDeformationComponent::CacheProxyState()
{
	CachedBoxes.Reset();
	CachedBoxes.Reserve(ConvexBoxes.Num());

	for (UBoxComponent* Box : ConvexBoxes)
	{
		if (!Box)
		{
			continue;
		}

		FBoxProxyCache Entry;
		Entry.Box = Box;
		Entry.InitialRelativeTransform = Box->GetRelativeTransform();
		Entry.InitialExtent = Box->GetUnscaledBoxExtent();
		Entry.InitialCornersLocal = BuildCorners(Entry.InitialExtent);
		Entry.InitialCornersMeshLocalF.SetNumUninitialized(Entry.InitialCornersLocal.Num());
		Entry.DeformedCornersMeshLocalF.SetNumUninitialized(Entry.InitialCornersLocal.Num());

		for (int32 CornerIndex = 0; CornerIndex < Entry.InitialCornersLocal.Num(); ++CornerIndex)
		{
			const FVector CornerInMeshLocal = Entry.InitialRelativeTransform.TransformPosition(Entry.InitialCornersLocal[CornerIndex]);
			Entry.InitialCornersMeshLocalF[CornerIndex] = FVector3f(CornerInMeshLocal);
			Entry.DeformedCornersMeshLocalF[CornerIndex] = FVector3f(CornerInMeshLocal);
		}

		CachedBoxes.Add(MoveTemp(Entry));
	}
}

FVector UCarMeshDeformationComponent::EvaluateDentOffset(const FVector& LocalPoint) const
{
	FVector Offset = FVector::ZeroVector;

	for (const FRuntimeDent& Dent : RuntimeDents)
	{
		if (Dent.CurrentDepth <= 0.05f)
		{
			continue;
		}

		const float Distance = FVector::Distance(LocalPoint, Dent.LocalCenter);
		if (Distance >= Dent.Radius)
		{
			continue;
		}

		const float T = 1.0f - (Distance / Dent.Radius);
		const float Falloff = T * T;
		Offset += (-Dent.LocalNormal) * Dent.CurrentDepth * Falloff;
	}

	return Offset;
}

void UCarMeshDeformationComponent::EvaluateProxyVerticesLowLevel(FBoxProxyCache& Cached, FVector& OutMin, FVector& OutMax) const
{
	OutMin = FVector(FLT_MAX);
	OutMax = FVector(-FLT_MAX);

	FVector3f* RESTRICT DeformedPtr = Cached.DeformedCornersMeshLocalF.GetData();
	const FVector3f* RESTRICT InitialPtr = Cached.InitialCornersMeshLocalF.GetData();
	const int32 VertexCount = Cached.InitialCornersMeshLocalF.Num();

	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const FVector SourceVertex = FVector(InitialPtr[VertexIndex]);
		const FVector DeformedVertex = SourceVertex + EvaluateDentOffset(SourceVertex);
		DeformedPtr[VertexIndex] = FVector3f(DeformedVertex);

		const FVector BackInBoxLocal = Cached.InitialRelativeTransform.InverseTransformPosition(DeformedVertex);
		OutMin.X = FMath::Min(OutMin.X, BackInBoxLocal.X);
		OutMin.Y = FMath::Min(OutMin.Y, BackInBoxLocal.Y);
		OutMin.Z = FMath::Min(OutMin.Z, BackInBoxLocal.Z);
		OutMax.X = FMath::Max(OutMax.X, BackInBoxLocal.X);
		OutMax.Y = FMath::Max(OutMax.Y, BackInBoxLocal.Y);
		OutMax.Z = FMath::Max(OutMax.Z, BackInBoxLocal.Z);
	}
}

void UCarMeshDeformationComponent::UpdateCollisionProxiesBudgeted(float DeltaTime)
{
	if (CachedBoxes.Num() == 0)
	{
		return;
	}

	CollisionUpdateTimer += DeltaTime;
	if (CollisionUpdateTimer < CollisionUpdateInterval)
	{
		return;
	}
	CollisionUpdateTimer = 0.0f;

	const int32 Iterations = FMath::Min(MaxProxyUpdatesPerTick, CachedBoxes.Num());
	for (int32 UpdateIdx = 0; UpdateIdx < Iterations; ++UpdateIdx)
	{
		FBoxProxyCache& Cached = CachedBoxes[NextProxyIndex];
		NextProxyIndex = (NextProxyIndex + 1) % CachedBoxes.Num();

		if (!Cached.Box)
		{
			continue;
		}

		FVector Min;
		FVector Max;
		if (bUseLowLevelCollisionVertexPath)
		{
			EvaluateProxyVerticesLowLevel(Cached, Min, Max);
		}
		else
		{
			Min = FVector(FLT_MAX);
			Max = FVector(-FLT_MAX);
			for (const FVector& BoxCorner : Cached.InitialCornersLocal)
			{
				const FVector CornerInMeshLocal = Cached.InitialRelativeTransform.TransformPosition(BoxCorner);
				const FVector DeformedCorner = CornerInMeshLocal + EvaluateDentOffset(CornerInMeshLocal);
				const FVector BackInBoxLocal = Cached.InitialRelativeTransform.InverseTransformPosition(DeformedCorner);

				Min.X = FMath::Min(Min.X, BackInBoxLocal.X);
				Min.Y = FMath::Min(Min.Y, BackInBoxLocal.Y);
				Min.Z = FMath::Min(Min.Z, BackInBoxLocal.Z);
				Max.X = FMath::Max(Max.X, BackInBoxLocal.X);
				Max.Y = FMath::Max(Max.Y, BackInBoxLocal.Y);
				Max.Z = FMath::Max(Max.Z, BackInBoxLocal.Z);
			}
		}

		const FVector NewExtent = (Max - Min) * 0.5f;
		const FVector LocalOffsetInBox = (Max + Min) * 0.5f;
		const FVector MeshLocalOffset = Cached.InitialRelativeTransform.TransformVector(LocalOffsetInBox);

		FTransform NewTransform = Cached.InitialRelativeTransform;
		NewTransform.AddToTranslation(MeshLocalOffset);
		Cached.Box->SetRelativeTransform(NewTransform);
		Cached.Box->SetBoxExtent(NewExtent.ComponentMax(FVector(1.0f, 1.0f, 1.0f)), true);
	}
}
