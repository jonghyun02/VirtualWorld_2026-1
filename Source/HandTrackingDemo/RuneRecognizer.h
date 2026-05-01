#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "RuneTypes.h"
#include "RuneRecognizer.generated.h"

/**
 * 룬 도형 인식기. 정적 메서드 기반 순수 함수 컬렉션.
 *
 * 인식 파이프라인:
 *   1) RecognizeHeuristic — 원형도, 곡률 피크, 종횡비 기반 빠른 분류
 *   2) Heuristic confidence < 0.7 이면 RecognizeOneDollar 호출 ($1 Unistroke)
 *   3) Recognize 가 위 두 결과를 dual-confidence 결합
 *
 * 6종 도형: Circle, Line, Triangle, VShape, Zigzag, Star
 *
 * 입력: 2D 정사영된 폴리라인 (FVector2D 배열)
 * 출력: EShapeType + confidence (0..1)
 */
UCLASS()
class HANDTRACKINGDEMO_API URuneRecognizer : public UObject
{
	GENERATED_BODY()

public:
	/** 휴리스틱 인식. 빠르고 직관적. */
	UFUNCTION(BlueprintCallable, Category = "Rune|Recognition")
	static EShapeType RecognizeHeuristic(const TArray<FVector2D>& Stroke, float& OutConfidence);

	/**
	 * $1 Unistroke Recognizer (Wobbrock et al., 2007).
	 * MIT 라이선스 (https://depts.washington.edu/acelab/proj/dollar/index.html).
	 * adaptive resampling: 입력 길이에 따라 32/64/96 점.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rune|Recognition")
	static EShapeType RecognizeOneDollar(const TArray<FVector2D>& Stroke, float& OutConfidence, bool bDisableStar = false);

	/**
	 * 통합 진입점. 휴리스틱 → confidence < 0.7 시 $1 → dual confidence 결합.
	 *  - 두 결과 shape 일치: harmonic mean
	 *  - 불일치: 마진 0.15 이상이면 우세 측 채택, 아니면 None
	 *  - 둘 다 < 0.5 면 None
	 */
	UFUNCTION(BlueprintCallable, Category = "Rune|Recognition")
	static EShapeType Recognize(
		const TArray<FVector2D>& Stroke,
		float& OutConfidence,
		bool bUseHeuristic = true,
		bool bUseFallback = true,
		bool bDisableStar = false);

	/** $1 템플릿을 BeginPlay 등에서 1회 캐시 빌드. 멱등. */
	static void EnsureTemplatesBuilt();

	/** 진단/디버그용. EShapeType → FString. */
	static FString ShapeToString(EShapeType Shape);

private:
	// --- 휴리스틱 내부 헬퍼 ---
	static void NormalizeStroke(const TArray<FVector2D>& In, TArray<FVector2D>& Out, FVector2D& OutBBoxSize);
	static float ComputePolygonArea(const TArray<FVector2D>& Stroke);
	static float ComputePerimeter(const TArray<FVector2D>& Stroke);
	static int32 CountCurvaturePeaks(const TArray<FVector2D>& Stroke, float AngleThresholdDeg = 50.0f);

	// --- $1 내부 헬퍼 ---
	static void Resample(const TArray<FVector2D>& In, TArray<FVector2D>& Out, int32 N);
	static void RotateToZero(TArray<FVector2D>& Stroke);
	static void ScaleToSquare(TArray<FVector2D>& Stroke, float Size);
	static void TranslateToOrigin(TArray<FVector2D>& Stroke);
	static float PathDistance(const TArray<FVector2D>& A, const TArray<FVector2D>& B);
	static float DistanceAtBestAngle(const TArray<FVector2D>& Stroke, const TArray<FVector2D>& Tmpl);
	static float Indicative_Angle(const TArray<FVector2D>& Stroke);

	// --- 템플릿 ---
	struct FOneDollarTemplate
	{
		EShapeType Shape;
		TArray<FVector2D> Points; // resampled to 64 points
	};
	static TArray<FOneDollarTemplate> Templates;
	static bool bTemplatesBuilt;

	static void BuildTemplate(EShapeType Shape, const TArray<FVector2D>& RawPoints);
	static TArray<FVector2D> MakeCircleTemplate(int32 N = 64);
	static TArray<FVector2D> MakeLineTemplate(int32 N = 64);
	static TArray<FVector2D> MakeTriangleTemplate(int32 N = 64);
	static TArray<FVector2D> MakeVShapeTemplate(int32 N = 64);
	static TArray<FVector2D> MakeZigzagTemplate(int32 N = 64);
	static TArray<FVector2D> MakeStarTemplate(int32 N = 64);
};
