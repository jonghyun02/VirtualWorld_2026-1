#include "RuneDrawingComponent.h"
#include "RuneRecognizer.h"

#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PoseableMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

// OculusXR — UE 5.7.4 Meta XR plugin
//   GetBoneTransform: UPoseableMeshComponent 상속. EBoneSpaces::WorldSpace 기본.
//   IsHandPinching / GetFingerPinchStrength: UOculusXRInputFunctionLibrary 정적.
//   EOculusXRBone::HandIndex3 = 검지 끝 (UE 5.7 기준).
// 시그니처는 spike 단계(US-1)에서 검증. 어긋나면 본 이름 fallback("Hand_IndexTip") 사용.
#include "OculusXRHandComponent.h"
#include "OculusXRInputFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuneDraw, Log, All);

// ============================================================
//  Console variables (5중 의존성 디버깅 disable flags)
// ============================================================

static TAutoConsoleVariable<int32> CVarUseHeuristic(
	TEXT("r.RuneDraw.UseHeuristic"), 1,
	TEXT("Enable heuristic recognizer (1) or skip (0)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarUseFallback(
	TEXT("r.RuneDraw.UseFallback"), 1,
	TEXT("Enable $1 fallback recognizer (1) or skip (0)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarPlaneCheck(
	TEXT("r.RuneDraw.PlaneCheck"), 1,
	TEXT("Reject strokes when plane normal angle exceeds threshold (1) or accept all (0)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarContinuity(
	TEXT("r.RuneDraw.Continuity"), 1,
	TEXT("Honor 400ms continuity window after pinch release (1) or recognize immediately (0)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarPlanarSnap(
	TEXT("r.RuneDraw.PlanarSnap"), 1,
	TEXT("Project stroke to 2D HMD plane (1) or use raw 3D distances (0)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarDisableStar(
	TEXT("r.RuneDraw.DisableStar"), 0,
	TEXT("Escape hatch: drop Star from candidate set (1) when accuracy is too low."),
	ECVF_Default);

// ============================================================
//  Bone helpers — Meta XR plugin이 만드는 OpenXR 핸드 스켈레톤은
//  EOculusXRBone enum의 DisplayName을 본 이름으로 그대로 씀
//  (예: "Index Tip", "Middle Tip"). GetBoneName이 진실의 원천.
// ============================================================

static FName ResolveBoneName(EOculusXRBone Bone)
{
	const FString DisplayName = UOculusXRInputFunctionLibrary::GetBoneName(Bone);
	return FName(*DisplayName);
}

static bool TryFindBoneWorld(UOculusXRHandComponent* Hand, EOculusXRBone Bone, FVector& OutWorld)
{
	if (!Hand) return false;
	const FName Name = ResolveBoneName(Bone);
	if (!Name.IsNone())
	{
		const int32 Idx = Hand->GetBoneIndex(Name);
		if (Idx != INDEX_NONE)
		{
			OutWorld = Hand->GetBoneTransformByName(Name, EBoneSpaces::WorldSpace).GetLocation();
			return true;
		}
	}
	return false;
}

// ============================================================

URuneDrawingComponent::URuneDrawingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after hand pose updated
	bAutoActivate = true;
}

void URuneDrawingComponent::BeginPlay()
{
	Super::BeginPlay();

	URuneRecognizer::EnsureTemplatesBuilt();

	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			CachedCameraMgr = PC->PlayerCameraManager;
		}
	}

	SetupHandRefIfNeeded();

	UE_LOG(LogRuneDraw, Log, TEXT("[INIT] Hand=%s HandRef=%s"),
		SideName(), HandRef ? *HandRef->GetName() : TEXT("<none>"));

	// 스켈레톤 dump는 본이 처음 생긴 프레임에 시도 (TickComponent 참조).
}

void URuneDrawingComponent::SetupHandRefIfNeeded()
{
	if (HandRef) return;
	AActor* Owner = GetOwner();
	if (!Owner) return;

	// Find UOculusXRHandComponent on owner matching HandSide.
	TArray<UOculusXRHandComponent*> Hands;
	Owner->GetComponents<UOculusXRHandComponent>(Hands);
	for (UOculusXRHandComponent* H : Hands)
	{
		if (!H) continue;
		const FName HName = H->GetFName();
		const bool bIsLeft = HName.ToString().Contains(TEXT("Left"));
		const bool bIsRight = HName.ToString().Contains(TEXT("Right"));
		if (HandSide == EHandSide::Left && bIsLeft)  { HandRef = H; break; }
		if (HandSide == EHandSide::Right && bIsRight) { HandRef = H; break; }
	}
}

void URuneDrawingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!HandRef)
	{
		SetupHandRefIfNeeded();
		if (!HandRef) return;
	}

	// OpenXR session 시작 후에야 hand skeletal mesh가 만들어짐. 첫 프레임에 dump.
	if (!bBonesDumped)
	{
		const int32 NumBones = HandRef->GetNumBones();
		if (NumBones > 0)
		{
			UE_LOG(LogRuneDraw, Log, TEXT("[BONES] Hand=%s NumBones=%d"), SideName(), NumBones);
			for (int32 i = 0; i < NumBones; ++i)
			{
				const FName BN = HandRef->GetBoneName(i);
				UE_LOG(LogRuneDraw, Log, TEXT("[BONES] Hand=%s [%2d]=\"%s\""), SideName(), i, *BN.ToString());
			}
			bBonesDumped = true;
		}
	}

	float PinchStrength = 0.0f;
	const bool bHavePinch = TryGetPinchStrength(PinchStrength);

	// Idle 상태에서도 손끝에 작은 indicator를 항상 그림 — 사용자가 핸드 트래킹이
	// 살아있는지, 어디에 점이 찍힐지 즉시 확인 가능. 핀치 중에는 채도 높은 색.
	if (UWorld* W = GetWorld())
	{
		FVector Tip;
		if (TryGetIndexTipWorld(Tip))
		{
			const bool bDrawing = (State == EDrawState::Drawing);
			const FColor IdleColor   = (HandSide == EHandSide::Left) ? FColor(0, 80, 80)   : FColor(80, 0, 80);
			const FColor ActiveColor = (HandSide == EHandSide::Left) ? FColor::Cyan        : FColor::Magenta;
			const FColor C = bDrawing ? ActiveColor : IdleColor;
			DrawDebugSphere(W, Tip, bDrawing ? 1.8f : 1.0f, 12, C, false, 0.0f, 0, 0.3f);
		}
	}

	// 1단계: Schmitt trigger 히스테리시스.
	bool bRawPinch = bHavePinch
		? (bWasPinching ? PinchStrength > PinchEndThreshold : PinchStrength > PinchStartThreshold)
		: false;

	// 2단계: release debounce — 핀치였다가 raw가 떨어졌어도 200ms 연속 떨어져야 진짜 release.
	// 단발 jitter (한두 프레임 동안 strength가 임계값 밑으로) 흡수.
	if (bWasPinching && !bRawPinch)
	{
		PinchOffTimer += DeltaTime;
		if (PinchOffTimer < PinchOffDebounceSec)
		{
			bRawPinch = true; // 핀치 유지로 간주.
		}
	}
	else
	{
		PinchOffTimer = 0.0f;
	}

	const bool bPinchOn = bRawPinch;

	// --- pinch edge detection ---
	if (bPinchOn && !bWasPinching)
	{
		// pinch start
		const bool bWithinContinuity = (CVarContinuity.GetValueOnGameThread() != 0)
			&& (State == EDrawState::Idle && ReleaseTimer > 0.0f && ReleaseTimer < ContinuityWindowSec)
			&& (Stroke3D.Num() >= MinSamples);
		if (!bWithinContinuity)
		{
			BeginSession();
		}
		else
		{
			UE_LOG(LogRuneDraw, Log, TEXT("[FSM] Hand=%s ContinueStroke samples=%d"),
				SideName(), Stroke3D.Num());
			State = EDrawState::Drawing;
		}
		ReleaseTimer = 0.0f;
	}
	else if (!bPinchOn && bWasPinching)
	{
		// pinch release
		UE_LOG(LogRuneDraw, Log, TEXT("[FSM] Hand=%s Drawing->ReleaseWait samples=%d"),
			SideName(), Stroke3D.Num());
		ReleaseTimer = 0.0f;
	}
	bWasPinching = bPinchOn;

	// --- state transitions / sampling ---
	switch (State)
	{
		case EDrawState::Drawing:
		{
			StateTimer += DeltaTime;

			// 60Hz sampling
			SampleAccumulator += DeltaTime;
			const float SampleInterval = 1.0f / FMath::Max(1.0f, SampleRateHz);
			while (SampleAccumulator >= SampleInterval && Stroke3D.Num() < MaxSamples)
			{
				FVector World;
				if (TryGetIndexTipWorld(World))
				{
					AppendSample(World);
				}
				SampleAccumulator -= SampleInterval;
				HzWindowSamples++;
			}

			// Hz measurement window (rolling)
			HzWindowAccum += DeltaTime;
			if (HzWindowAccum >= 1.0f)
			{
				MeasuredHz = static_cast<float>(HzWindowSamples) / HzWindowAccum;
				UE_LOG(LogRuneDraw, Verbose, TEXT("[HZ] Hand=%s IndexTip=%.1fHz"), SideName(), MeasuredHz);
				HzWindowAccum = 0.0f;
				HzWindowSamples = 0;
			}

			// hard cap
			if (StateTimer >= HardCapSec)
			{
				UE_LOG(LogRuneDraw, Log, TEXT("[FSM] Hand=%s HardCap reached, forcing recognize"), SideName());
				EndSessionAndRecognize();
			}

			// ongoing release timer (only after pinch goes off)
			if (!bPinchOn)
			{
				ReleaseTimer += DeltaTime;
				const float Window = (CVarContinuity.GetValueOnGameThread() != 0) ? ContinuityWindowSec : 0.0f;
				if (ReleaseTimer >= Window)
				{
					EndSessionAndRecognize();
				}
			}

			if (bDrawDebugTrail) DrawTrailDebug();
			break;
		}
		case EDrawState::Idle:
			ReleaseTimer = 0.0f;
			break;
		case EDrawState::Recognizing:
			// transient — recognize+broadcast happens synchronously, so we should not stay here
			State = EDrawState::Idle;
			break;
	}

	// --- diagnostic log throttle ---
	DiagLogAccum += DeltaTime;
	if (DiagLogAccum >= 2.0f)
	{
		DiagLogAccum = 0.0f;
		FVector Tip;
		if (TryGetIndexTipWorld(Tip))
		{
			UE_LOG(LogRuneDraw, Verbose,
				TEXT("[DIAG] Hand=%s State=%d Pinch=%.2f Tip=(%.1f,%.1f,%.1f) Hz=%.1f"),
				SideName(), static_cast<int32>(State), PinchStrength,
				Tip.X, Tip.Y, Tip.Z, MeasuredHz);
		}
	}
}

// ============================================================
//  Hand API access (US-1 spike-validated paths)
// ============================================================

bool URuneDrawingComponent::TryGetIndexTipWorld(FVector& OutWorld)
{
	if (!HandRef) return false;

	// 1순위: 검지 끝 (Index_Tip → "Index Tip")
	if (TryFindBoneWorld(HandRef, EOculusXRBone::Index_Tip, OutWorld)) return true;
	// 2순위: 검지 마지막 관절 (Index_3 → "Index3")
	if (TryFindBoneWorld(HandRef, EOculusXRBone::Index_3, OutWorld)) return true;

	OutWorld = HandRef->GetComponentLocation();
	return true;
}

bool URuneDrawingComponent::TryGetPinchStrength(float& OutStrength)
{
	OutStrength = 0.0f;
	if (!HandRef) return false;

	// 엄지+가운데 손가락 핀치 — Quest 3 시스템 메뉴(엄지+검지)와 충돌 회피.
	// 본 이름은 EOculusXRBone enum DisplayName이 진실의 원천: "Thumb Tip", "Middle Tip".
	FVector Thumb, Middle;
	const bool bHaveThumb  = TryFindBoneWorld(HandRef, EOculusXRBone::Thumb_Tip,  Thumb)
	                      || TryFindBoneWorld(HandRef, EOculusXRBone::Thumb_3,    Thumb);
	const bool bHaveMiddle = TryFindBoneWorld(HandRef, EOculusXRBone::Middle_Tip, Middle)
	                      || TryFindBoneWorld(HandRef, EOculusXRBone::Middle_3,   Middle);
	if (!bHaveThumb || !bHaveMiddle)
	{
		// 본 매칭 실패 — strength=0 유지 (영구 핀치 false-trigger 방지).
		return false;
	}

	const float Dist = FVector::Distance(Thumb, Middle);
	// Thumb-middle 거리 → strength 매핑. 핸드트래킹 노이즈가 ±1cm 정도 있어서
	// 너그럽게 매핑: 2cm 이하 = 1.0, 9cm 이상 = 0.0. 임계값 (start 0.7, end 0.2)과
	// 합쳐서 핀치 유지 거리 ~7.6cm까지 OK.
	OutStrength = FMath::Clamp(1.0f - (Dist - 2.0f) / 7.0f, 0.0f, 1.0f);
	return true;
}

// ============================================================
//  Session lifecycle
// ============================================================

void URuneDrawingComponent::BeginSession()
{
	Stroke3D.Reset();
	Stroke2D.Reset();
	bRejectStroke = false;
	StateTimer = 0.0f;
	ReleaseTimer = 0.0f;
	SampleAccumulator = 0.0f;
	HzWindowAccum = 0.0f;
	HzWindowSamples = 0;
	State = EDrawState::Drawing;
	OnTrailReset();

	CapturePlaneFromHMD();
	UE_LOG(LogRuneDraw, Log, TEXT("[FSM] Hand=%s Idle->Drawing plane R.U=%.4f"),
		SideName(),
		FVector::DotProduct(PlaneRight, PlaneUp));
}

void URuneDrawingComponent::EndSessionAndRecognize()
{
	State = EDrawState::Recognizing;

	if (Stroke3D.Num() < MinSamples)
	{
		UE_LOG(LogRuneDraw, Log, TEXT("[CAST] Hand=%s shape=None reason=too_few_samples (%d)"),
			SideName(), Stroke3D.Num());
		Stroke3D.Reset(); Stroke2D.Reset();
		State = EDrawState::Idle;
		return;
	}

	const bool bPlaneCheck = CVarPlaneCheck.GetValueOnGameThread() != 0;
	if (bPlaneCheck && !CheckPlaneNormalAngleOK())
	{
		UE_LOG(LogRuneDraw, Warning,
			TEXT("[CAST] Hand=%s shape=None reason=plane_misaligned hint=draw_perpendicular_to_view"),
			SideName());
		bRejectStroke = true;
		Stroke3D.Reset(); Stroke2D.Reset();
		State = EDrawState::Idle;
		return;
	}

	const bool bUseHeur = CVarUseHeuristic.GetValueOnGameThread() != 0;
	const bool bUseFall = CVarUseFallback.GetValueOnGameThread() != 0;
	const bool bDisableStar = CVarDisableStar.GetValueOnGameThread() != 0;

	float Confidence = 0.0f;
	const EShapeType Shape = URuneRecognizer::Recognize(
		Stroke2D, Confidence, bUseHeur, bUseFall, bDisableStar);

	UE_LOG(LogRuneDraw, Log, TEXT("[CAST] Hand=%s label=%s confidence=%.3f samples=%d"),
		SideName(),
		*URuneRecognizer::ShapeToString(Shape),
		Confidence,
		Stroke3D.Num());

	if (GEngine)
	{
		const FColor Tint = (Shape == EShapeType::None) ? FColor::Red : ((HandSide == EHandSide::Left) ? FColor::Cyan : FColor::Magenta);
		const FString Msg = FString::Printf(TEXT("[%s] %s  conf=%.2f  samples=%d"),
			SideName(), *URuneRecognizer::ShapeToString(Shape), Confidence, Stroke3D.Num());
		GEngine->AddOnScreenDebugMessage(static_cast<int32>(HandSide) + 100, 4.0f, Tint, Msg);
	}

	if (Shape != EShapeType::None)
	{
		FShapeRecognitionResult R;
		R.Shape = Shape;
		R.Confidence = Confidence;
		R.HandSide = HandSide;
		R.OriginWorld = HandRef ? HandRef->GetComponentLocation() : FVector::ZeroVector;
		// Forward = HMD forward fallback, but prefer wrist forward
		if (HandRef)
		{
			R.ForwardWorld = HandRef->GetForwardVector();
		}
		OnShapeRecognized.Broadcast(R);
	}

	Stroke3D.Reset();
	Stroke2D.Reset();
	State = EDrawState::Idle;
}

void URuneDrawingComponent::AbortSession()
{
	Stroke3D.Reset();
	Stroke2D.Reset();
	State = EDrawState::Idle;
	OnTrailReset();
}

void URuneDrawingComponent::AppendSample(const FVector& WorldPoint)
{
	Stroke3D.Add(WorldPoint);

	if (CVarPlanarSnap.GetValueOnGameThread() != 0)
	{
		const FVector Local = WorldPoint - PlaneOrigin;
		const float U = FVector::DotProduct(Local, PlaneRight);
		const float V = FVector::DotProduct(Local, PlaneUp);
		Stroke2D.Add(FVector2D(U, V));
	}
	else
	{
		// raw 2D = world XY
		Stroke2D.Add(FVector2D(WorldPoint.X, WorldPoint.Z));
	}

	OnTrailPointAdded(WorldPoint);
}

void URuneDrawingComponent::CapturePlaneFromHMD()
{
	if (!CachedCameraMgr) return;

	const FVector Loc = CachedCameraMgr->GetCameraLocation();
	const FRotator Rot = CachedCameraMgr->GetCameraRotation();
	const FVector Forward = Rot.Vector();
	const FVector Right = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
	const FVector Up = FVector::CrossProduct(Right, Forward).GetSafeNormal();

	PlaneOrigin = Loc + Forward * 30.0f; // 30cm 앞 평면
	PlaneRight = Right;
	PlaneUp = Up;
	PlaneNormal = Forward;
}

bool URuneDrawingComponent::CheckPlaneNormalAngleOK() const
{
	// 30개 미만 샘플은 stroke normal 추정이 너무 노이즈 → 평면 체크 스킵.
	if (Stroke3D.Num() < 30) return true;

	// estimate stroke plane via SVD-lite: take centroid + average cross product of consecutive segments
	FVector Centroid = FVector::ZeroVector;
	for (const FVector& P : Stroke3D) Centroid += P;
	Centroid /= static_cast<float>(Stroke3D.Num());

	FVector AvgNormal = FVector::ZeroVector;
	for (int32 i = 1; i + 1 < Stroke3D.Num(); ++i)
	{
		const FVector A = Stroke3D[i - 1] - Centroid;
		const FVector B = Stroke3D[i + 1] - Centroid;
		AvgNormal += FVector::CrossProduct(A, B);
	}
	const FVector StrokeNormal = AvgNormal.GetSafeNormal();
	if (StrokeNormal.IsNearlyZero()) return true;

	const float Dot = FMath::Abs(FVector::DotProduct(StrokeNormal, PlaneNormal));
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, 0.0f, 1.0f)));
	return AngleDeg <= PlaneNormalAngleDegThreshold;
}

void URuneDrawingComponent::DrawTrailDebug() const
{
	UWorld* W = GetWorld();
	if (!W || Stroke3D.Num() < 1) return;
	const FColor Color = (HandSide == EHandSide::Left) ? FColor::Cyan : FColor::Magenta;

	// 1) 트레일 전체를 매 프레임 다시 그림 (LifeTime=0으로 한 프레임만 — 다음 프레임에 또 그림)
	for (int32 i = 1; i < Stroke3D.Num(); ++i)
	{
		DrawDebugLine(W, Stroke3D[i - 1], Stroke3D[i], Color, false, 0.0f, 0, 1.5f);
	}

	// 2) 손끝에 큰 빛나는 오브 — "지금 그리고 있다" 신호. 항상 보이게 LifeTime=0.
	const FVector& Last = Stroke3D.Last();
	DrawDebugSphere(W, Last, 2.5f, 12, Color, false, 0.0f, 0, 0.5f);
}
