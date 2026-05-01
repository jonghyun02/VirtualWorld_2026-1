#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RuneTypes.h"
#include "RuneDrawingComponent.generated.h"

class UOculusXRHandComponent;
class APlayerCameraManager;

/**
 * 룬 드로잉 per-hand 컴포넌트.
 *
 * BP_HandPawn 에서 LeftHand/RightHand 별로 1개씩 부착. HandSide 변수로 좌/우 구분.
 *
 * 책임:
 *  1) 매 Tick OculusXR 핸드의 검지 끝(EOculusXRBone::HandIndex3) 월드 위치 가져오기
 *  2) 핀치(엄지+검지 접촉) 시작/종료 감지 → FSM (Idle/Drawing/Recognizing)
 *  3) 60Hz throttle로 점 샘플링 + 시작 시점 HMD 평면에 정사영
 *  4) 핀치 종료 → 400ms continuity window → 미발생 시 Recognizer 호출
 *  5) 1.5s hard cap
 *  6) 평면 normal vs HMD plane normal 60도 초과 시 REJECT
 *  7) FOnShapeRecognized delegate broadcast
 *
 * Console vars (디버깅 disable flags):
 *   r.RuneDraw.UseHeuristic, r.RuneDraw.UseFallback, r.RuneDraw.PlaneCheck
 *   r.RuneDraw.Continuity, r.RuneDraw.PlanarSnap, r.RuneDraw.DisableStar
 */
UCLASS(ClassGroup = (Rune), meta = (BlueprintSpawnableComponent), Blueprintable)
class HANDTRACKINGDEMO_API URuneDrawingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URuneDrawingComponent();

	/** 양손 중 어느 쪽인지. BP_HandPawn 에서 설정. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Setup")
	EHandSide HandSide = EHandSide::Left;

	/** 자동 바인딩될 OculusXRHandComponent. 비워두면 BeginPlay에서 owner의 hand를 찾음. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Setup")
	TObjectPtr<UOculusXRHandComponent> HandRef;

	/** 핀치 시작 임계값 (FingerPinchStrength 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Tuning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PinchStartThreshold = 0.7f;

	/** 핀치 종료 임계값 (히스테리시스). 손가락 트래킹 jitter 흡수를 위해 매우 lenient하게. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Tuning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PinchEndThreshold = 0.2f;

	/** 샘플링 레이트 Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Tuning", meta = (ClampMin = "10.0", ClampMax = "120.0"))
	float SampleRateHz = 60.0f;

	/** 핀치 release 후 같은 stroke로 잇기 위한 대기 시간(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Tuning", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ContinuityWindowSec = 0.4f;

	/** stroke 최대 지속 시간(초). 천천히 그리는 사용자 위해 길게. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Tuning", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float HardCapSec = 4.0f;

	/** 평면 normal 각도 임계값(도). 초과 시 stroke REJECT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Tuning", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float PlaneNormalAngleDegThreshold = 60.0f;

	/** 디버그 트레일 표시 여부. BP에서 Niagara로 후속 교체할 때 false로. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Debug")
	bool bDrawDebugTrail = true;

	/** 인식 결과 broadcast. URuneSpellComponent가 바인드. */
	UPROPERTY(BlueprintAssignable, Category = "Rune|Events")
	FOnShapeRecognized OnShapeRecognized;

	/**
	 * 트레일에 점이 추가될 때 BP에 알림. Niagara Ribbon 등에 바인딩하면 시각 트레일 구현 가능.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Rune|Visual")
	void OnTrailPointAdded(FVector WorldPoint);

	UFUNCTION(BlueprintImplementableEvent, Category = "Rune|Visual")
	void OnTrailReset();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Rune|Diagnostics")
	float GetMeasuredHz() const { return MeasuredHz; }

	UFUNCTION(BlueprintCallable, Category = "Rune|Diagnostics")
	int32 GetSampleCount() const { return Stroke3D.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Rune|Diagnostics")
	EDrawState GetState() const { return State; }

private:
	// --- runtime state ---
	UPROPERTY(Transient)
	EDrawState State = EDrawState::Idle;

	UPROPERTY(Transient)
	TObjectPtr<APlayerCameraManager> CachedCameraMgr;

	// per-session sampling state
	float SampleAccumulator = 0.0f;
	float StateTimer = 0.0f;
	float ReleaseTimer = 0.0f;
	bool bWasPinching = false;
	bool bRejectStroke = false;

	// 핀치 release debounce: jitter로 strength가 잠깐 임계값 밑으로 떨어진 게
	// 진짜 release인지 확인하기 위해 200ms 동안 지속되는지 봄.
	float PinchOffTimer = 0.0f;
	static constexpr float PinchOffDebounceSec = 0.20f;

	// raw 3D and projected 2D strokes
	TArray<FVector> Stroke3D;
	TArray<FVector2D> Stroke2D;
	int32 MaxSamples = 512;
	int32 MinSamples = 8;

	// session plane (captured at session start)
	FVector PlaneOrigin = FVector::ZeroVector;
	FVector PlaneRight = FVector::RightVector;
	FVector PlaneUp = FVector::UpVector;
	FVector PlaneNormal = FVector::ForwardVector;

	// Hz measurement (US-1 spike + ongoing diagnostic)
	float MeasuredHz = 0.0f;
	float HzWindowAccum = 0.0f;
	int32 HzWindowSamples = 0;

	// log throttling
	float DiagLogAccum = 0.0f;

	// OculusXR HandComponent는 OpenXR session 시작 후에야 skeletal mesh를 만듦
	// (BeginPlay 시점엔 NumBones=0). 본이 처음 생긴 프레임에 1회 dump.
	bool bBonesDumped = false;

	void SetupHandRefIfNeeded();
	// non-const: GetBoneTransformByName(UPoseableMeshComponent)이 non-const 호출.
	bool TryGetIndexTipWorld(FVector& OutWorld);
	bool TryGetPinchStrength(float& OutStrength);

	void BeginSession();
	void EndSessionAndRecognize();
	void AbortSession();
	void AppendSample(const FVector& WorldPoint);
	void CapturePlaneFromHMD();
	bool CheckPlaneNormalAngleOK() const;
	void DrawTrailDebug() const;

	const TCHAR* SideName() const { return HandSide == EHandSide::Left ? TEXT("L") : TEXT("R"); }
};
