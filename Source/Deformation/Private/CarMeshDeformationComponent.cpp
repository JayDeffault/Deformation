#include "CarMeshDeformationComponent.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
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
	FCarRHIDentUploader()
		: SharedState(MakeShared<FState, ESPMode::ThreadSafe>())
	{
	}

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

		const uint32 SafeMaxDentCount = FMath::Max(1u, MaxDentCount);
		TSharedPtr<FState, ESPMode::ThreadSafe> State = SharedState;
		ENQUEUE_RENDER_COMMAND(InitDentBuffer)(
			[State, SafeMaxDentCount](FRHICommandListImmediate& RHICmdList)
			{
				if (!State.IsValid())
				{
					return;
				}

				const uint32 Bytes = SafeMaxDentCount * static_cast<uint32>(sizeof(FRHIDentPayload));
				FRHIResourceCreateInfo CreateInfo(TEXT("CarDentPayloadBuffer"));
				State->DentBuffer = RHICmdList.CreateVertexBuffer(Bytes, static_cast<EBufferUsageFlags>(BUF_Dynamic | BUF_ShaderResource), CreateInfo);
				State->Capacity = SafeMaxDentCount;
			});
	}

	void Upload(const TArray<FRuntimeDent>& RuntimeDents, uint32 MaxDentCount)
	{
		if (!IsInGameThread())
		{
			return;
		}

		const uint32 SafeMaxDentCount = FMath::Max(1u, MaxDentCount);
		TArray<FRHIDentPayload> Payload;
		Payload.Reserve(SafeMaxDentCount);

		const int32 CountToCopy = FMath::Min(static_cast<int32>(SafeMaxDentCount), RuntimeDents.Num());
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

		Payload.SetNum(SafeMaxDentCount);

		TSharedPtr<FState, ESPMode::ThreadSafe> State = SharedState;
		ENQUEUE_RENDER_COMMAND(UploadDentBuffer)(
			[State, SafeMaxDentCount, Payload = MoveTemp(Payload)](FRHICommandListImmediate& RHICmdList)
			{
				if (!State.IsValid())
				{
					return;
				}

				if (!State->DentBuffer.IsValid() || State->Capacity != SafeMaxDentCount)
				{
					const uint32 Bytes = SafeMaxDentCount * static_cast<uint32>(sizeof(FRHIDentPayload));
					FRHIResourceCreateInfo CreateInfo(TEXT("CarDentPayloadBuffer"));
					State->DentBuffer = RHICmdList.CreateVertexBuffer(Bytes, static_cast<EBufferUsageFlags>(BUF_Dynamic | BUF_ShaderResource), CreateInfo);
					State->Capacity = SafeMaxDentCount;
				}

				if (!State->DentBuffer.IsValid())
				{
					return;
				}

				const uint32 NumBytes = Payload.Num() * static_cast<uint32>(sizeof(FRHIDentPayload));
				void* Data = RHICmdList.LockBuffer(State->DentBuffer, 0, NumBytes, RLM_WriteOnly);
				FMemory::Memcpy(Data, Payload.GetData(), NumBytes);
				RHICmdList.UnlockBuffer(State->DentBuffer);
			});
	}

	void Release()
	{
		if (!IsInGameThread())
		{
			return;
		}

		TSharedPtr<FState, ESPMode::ThreadSafe> State = SharedState;
		ENQUEUE_RENDER_COMMAND(ReleaseDentBuffer)(
			[State](FRHICommandListImmediate& RHICmdList)
			{
				if (!State.IsValid())
				{
					return;
				}

				State->DentBuffer.SafeRelease();
				State->Capacity = 0;
			});
	}

private:
	struct FState
	{
		FBufferRHIRef DentBuffer;
		uint32 Capacity = 0;
	};

	TSharedPtr<FState, ESPMode::ThreadSafe> SharedState;
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

	CacheProxyState();
	InitializeProceduralVisualMesh();

	if (ProceduralVisualMesh && bDeformProceduralCollision)
	{
		ProceduralVisualMesh->OnComponentHit.AddDynamic(this, &UCarMeshDeformationComponent::OnMeshHit);
	}
	else if (VisualMesh)
	{
		VisualMesh->OnComponentHit.AddDynamic(this, &UCarMeshDeformationComponent::OnMeshHit);
	}

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
		RHIDentUploader.Reset();
	}

	if (ProceduralVisualMesh)
	{
		ProceduralVisualMesh->DestroyComponent();
		ProceduralVisualMesh = nullptr;
	}

	if (VisualMesh)
	{
		VisualMesh->SetVisibility(true, false);
		VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	Super::EndPlay(EndPlayReason);
}

void UCarMeshDeformationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TickDentSmoothing(DeltaTime);
	UpdateProceduralVisualMesh(DeltaTime);
	UploadDentsToRHI();

	if (bEnableCollisionProxyUpdate && !(bEnableProceduralVisualDeformation && bDeformProceduralCollision && ProceduralVisualMesh))
	{
		UpdateCollisionProxiesBudgeted(DeltaTime);
	}
}

void UCarMeshDeformationComponent::InitializeProceduralVisualMesh()
{
	if (!bEnableProceduralVisualDeformation || !VisualMesh || !VisualMesh->GetStaticMesh())
	{
		return;
	}

	if (ProceduralVisualMesh)
	{
		return;
	}

	UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(VisualMesh->GetStaticMesh(), 0, 0, BaseVisualVertices, VisualTriangles, VisualNormals, VisualUV0, VisualTangents);
	if (BaseVisualVertices.Num() == 0 || VisualTriangles.Num() == 0)
	{
		return;
	}

	DeformedVisualVertices = BaseVisualVertices;
	VisualColors.SetNumZeroed(BaseVisualVertices.Num());

	ProceduralVisualMesh = NewObject<UProceduralMeshComponent>(GetOwner(), TEXT("CarDeformedMesh"));
	if (!ProceduralVisualMesh)
	{
		return;
	}

	if (AActor* OwnerActor = GetOwner())
	{
		if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
		{
			ProceduralVisualMesh->SetupAttachment(RootComponent);
		}
	}
	ProceduralVisualMesh->SetRelativeTransform(VisualMesh->GetRelativeTransform());
	ProceduralVisualMesh->RegisterComponent();
	ProceduralVisualMesh->SetNotifyRigidBodyCollision(true);
	ProceduralVisualMesh->SetGenerateOverlapEvents(false);
	ProceduralVisualMesh->SetCollisionEnabled(bDeformProceduralCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	const int32 MaterialCount = VisualMesh->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		ProceduralVisualMesh->SetMaterial(MaterialIndex, VisualMesh->GetMaterial(MaterialIndex));
	}

	ProceduralVisualMesh->CreateMeshSection(0, DeformedVisualVertices, VisualTriangles, VisualNormals, VisualUV0, VisualColors, VisualTangents, bDeformProceduralCollision);
	VisualMesh->SetVisibility(false, false);
	if (bDeformProceduralCollision)
	{
		VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void UCarMeshDeformationComponent::UpdateProceduralVisualMesh(float DeltaTime)
{
	if (!bEnableProceduralVisualDeformation || !ProceduralVisualMesh || BaseVisualVertices.Num() == 0)
	{
		return;
	}

	VisualUpdateTimer += DeltaTime;
	if (VisualUpdateTimer < VisualMeshUpdateInterval)
	{
		return;
	}
	VisualUpdateTimer = 0.0f;

	bool bAnyChange = false;
	DeformedVisualVertices.SetNumUninitialized(BaseVisualVertices.Num());
	for (int32 i = 0; i < BaseVisualVertices.Num(); ++i)
	{
		const FVector Deformed = BaseVisualVertices[i] + EvaluateDentOffset(BaseVisualVertices[i]);
		DeformedVisualVertices[i] = Deformed;
		bAnyChange |= !Deformed.Equals(BaseVisualVertices[i], 0.1f);
	}

	if (!bAnyChange && RuntimeDents.Num() == 0)
	{
		return;
	}

	ProceduralVisualMesh->UpdateMeshSection(0, DeformedVisualVertices, VisualNormals, VisualUV0, VisualColors, VisualTangents);

	if (bDeformProceduralCollision)
	{
		CollisionSyncTimer += DeltaTime;
		if (CollisionSyncTimer >= CollisionSyncInterval)
		{
			CollisionSyncTimer = 0.0f;
			ProceduralVisualMesh->CreateMeshSection(0, DeformedVisualVertices, VisualTriangles, VisualNormals, VisualUV0, VisualColors, VisualTangents, true);
		}
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
	for (FRuntimeDent& Dent : RuntimeDents)
	{
		Dent.CurrentDepth = FMath::FInterpTo(Dent.CurrentDepth, Dent.TargetDepth, DeltaTime, DentSmoothSpeed);
		Dent.RemainingTime = FMath::Max(0.0f, Dent.RemainingTime);
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
			Existing.RemainingTime = TNumericLimits<float>::Max();
			return;
		}
	}

	if (RuntimeDents.Num() >= MaxRuntimeDents)
	{
		int32 ReplaceIndex = 0;
		float MinScore = TNumericLimits<float>::Max();
		for (int32 i = 0; i < RuntimeDents.Num(); ++i)
		{
			const float Score = RuntimeDents[i].TargetDepth;
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
	Dent.RemainingTime = TNumericLimits<float>::Max();
	MergeOrAddDent(Dent);
}

void UCarMeshDeformationComponent::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	const float ImpulseMagnitude = NormalImpulse.Size();
	if (ImpulseMagnitude < MinImpactForDent)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float Now = World->GetTimeSeconds();
		if ((Now - LastAcceptedHitTime) < HitCooldown)
		{
			return;
		}
		LastAcceptedHitTime = Now;
	}

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
