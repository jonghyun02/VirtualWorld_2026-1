#include "RuneRecognizer.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuneRecognizer, Log, All);

TArray<URuneRecognizer::FOneDollarTemplate> URuneRecognizer::Templates;
bool URuneRecognizer::bTemplatesBuilt = false;

namespace RuneRecMath
{
	static constexpr float OneDollarSquareSize = 250.0f;
	static constexpr float OneDollarHalfDiagonal = 0.5f * 1.41421356f * OneDollarSquareSize;
	static constexpr float OneDollarAngleRange = 45.0f;       // degrees
	static constexpr float OneDollarAnglePrecision = 2.0f;    // degrees
	static constexpr float Phi = 0.5f * (-1.0f + 1.41421356f * 1.41421356f);
	static const float GoldenRatioInv = 0.5f * (FMath::Sqrt(5.0f) - 1.0f); // 0.618...
}

// ============================================================
//  Public entry points
// ============================================================

EShapeType URuneRecognizer::Recognize(
	const TArray<FVector2D>& Stroke,
	float& OutConfidence,
	bool bUseHeuristic,
	bool bUseFallback,
	bool bDisableStar)
{
	OutConfidence = 0.0f;
	if (Stroke.Num() < 8)
	{
		return EShapeType::None;
	}

	float HeurConf = 0.0f;
	EShapeType HeurShape = EShapeType::None;
	if (bUseHeuristic)
	{
		HeurShape = RecognizeHeuristic(Stroke, HeurConf);
	}

	const bool bNeedFallback = bUseFallback && (!bUseHeuristic || HeurConf < 0.7f);
	float DollarConf = 0.0f;
	EShapeType DollarShape = EShapeType::None;
	if (bNeedFallback)
	{
		DollarShape = RecognizeOneDollar(Stroke, DollarConf, bDisableStar);
	}

	// dual-confidence combine
	if (!bUseHeuristic)
	{
		OutConfidence = DollarConf;
		return DollarShape;
	}
	if (!bUseFallback)
	{
		OutConfidence = HeurConf;
		return HeurShape;
	}

	if (HeurShape == DollarShape && HeurShape != EShapeType::None)
	{
		// harmonic mean
		const float HM = (HeurConf > 0.0f && DollarConf > 0.0f)
			? (2.0f * HeurConf * DollarConf) / (HeurConf + DollarConf)
			: 0.0f;
		OutConfidence = HM;
		return HeurShape;
	}

	// disagreement: margin 0.15 winner
	if (FMath::Abs(HeurConf - DollarConf) > 0.15f)
	{
		if (HeurConf > DollarConf)
		{
			OutConfidence = HeurConf - 0.1f;
			return HeurShape;
		}
		OutConfidence = DollarConf - 0.1f;
		return DollarShape;
	}

	// both weak
	if (HeurConf < 0.5f && DollarConf < 0.5f)
	{
		OutConfidence = FMath::Max(HeurConf, DollarConf);
		return EShapeType::None;
	}

	// ambiguous but at least one is decent: prefer $1 (template-based)
	OutConfidence = FMath::Max(HeurConf, DollarConf) - 0.1f;
	return DollarShape != EShapeType::None ? DollarShape : HeurShape;
}

EShapeType URuneRecognizer::RecognizeHeuristic(const TArray<FVector2D>& Stroke, float& OutConfidence)
{
	OutConfidence = 0.0f;
	if (Stroke.Num() < 8)
	{
		return EShapeType::None;
	}

	TArray<FVector2D> Norm;
	FVector2D BBoxSize;
	NormalizeStroke(Stroke, Norm, BBoxSize);

	const float Area = ComputePolygonArea(Norm);
	const float Perim = ComputePerimeter(Norm);
	const float Circularity = (Perim > KINDA_SMALL_NUMBER)
		? (4.0f * PI * Area) / (Perim * Perim)
		: 0.0f;

	const int32 PeakCount = CountCurvaturePeaks(Norm, 50.0f);

	const float MajorAxis = FMath::Max(BBoxSize.X, BBoxSize.Y);
	const float MinorAxis = FMath::Max(FMath::Min(BBoxSize.X, BBoxSize.Y), KINDA_SMALL_NUMBER);
	const float AspectRatio = MajorAxis / MinorAxis;
	const float BBoxRatio = MinorAxis / MajorAxis; // 1.0 = square

	// 1) Circle: high circularity
	if (Circularity > 0.75f)
	{
		OutConfidence = FMath::Clamp(Circularity, 0.0f, 1.0f);
		return EShapeType::Circle;
	}

	// 2) Line: extreme aspect ratio + low curvature
	if (AspectRatio > 8.0f && PeakCount <= 1)
	{
		const float LineConf = FMath::Clamp((AspectRatio - 4.0f) / 12.0f, 0.0f, 1.0f);
		OutConfidence = FMath::Max(0.7f, LineConf);
		return EShapeType::Line;
	}

	// 3) VShape: 1 sharp peak (V is one apex)
	if (PeakCount == 1 && AspectRatio < 8.0f)
	{
		OutConfidence = 0.75f;
		return EShapeType::VShape;
	}

	// 4) Triangle: 3 peaks
	if (PeakCount == 3)
	{
		OutConfidence = 0.8f;
		return EShapeType::Triangle;
	}

	// 5) Star: 5 peaks + roughly square bbox
	if (PeakCount == 5 && BBoxRatio > 0.6f)
	{
		OutConfidence = 0.78f;
		return EShapeType::Star;
	}

	// 6) Zigzag: 4-6 peaks
	if (PeakCount >= 4 && PeakCount <= 6 && AspectRatio > 1.5f)
	{
		OutConfidence = 0.72f;
		return EShapeType::Zigzag;
	}

	OutConfidence = 0.3f;
	return EShapeType::None;
}

EShapeType URuneRecognizer::RecognizeOneDollar(const TArray<FVector2D>& Stroke, float& OutConfidence, bool bDisableStar)
{
	OutConfidence = 0.0f;
	if (Stroke.Num() < 8)
	{
		return EShapeType::None;
	}

	EnsureTemplatesBuilt();

	// Adaptive intermediate N: longer strokes are pre-smoothed at higher density before
	// being downsampled to the canonical 64-point template space. Short strokes start at 32
	// to avoid distortion from interpolating between widely-spaced raw samples.
	const int32 N = (Stroke.Num() > 96) ? 96 : (Stroke.Num() > 48) ? 64 : 32;

	TArray<FVector2D> Cand;
	Resample(Stroke, Cand, N);
	RotateToZero(Cand);
	ScaleToSquare(Cand, RuneRecMath::OneDollarSquareSize);
	TranslateToOrigin(Cand);

	// Templates are stored at 64 points; downsample/upsample Cand (already normalized).
	TArray<FVector2D> Cand64;
	if (N == 64)
	{
		Cand64 = Cand;
	}
	else
	{
		Resample(Cand, Cand64, 64);
		// Cand was already rotated/scaled/translated; resample preserves geometry.
	}

	float BestDist = TNumericLimits<float>::Max();
	EShapeType BestShape = EShapeType::None;
	for (const FOneDollarTemplate& T : Templates)
	{
		if (bDisableStar && T.Shape == EShapeType::Star)
		{
			continue;
		}
		const float D = DistanceAtBestAngle(Cand64, T.Points);
		if (D < BestDist)
		{
			BestDist = D;
			BestShape = T.Shape;
		}
	}

	OutConfidence = FMath::Clamp(1.0f - BestDist / RuneRecMath::OneDollarHalfDiagonal, 0.0f, 1.0f);
	return BestShape;
}

void URuneRecognizer::EnsureTemplatesBuilt()
{
	if (bTemplatesBuilt) return;

	Templates.Reset();
	BuildTemplate(EShapeType::Circle,   MakeCircleTemplate(64));
	BuildTemplate(EShapeType::Line,     MakeLineTemplate(64));
	BuildTemplate(EShapeType::Triangle, MakeTriangleTemplate(64));
	BuildTemplate(EShapeType::VShape,   MakeVShapeTemplate(64));
	BuildTemplate(EShapeType::Zigzag,   MakeZigzagTemplate(64));
	BuildTemplate(EShapeType::Star,     MakeStarTemplate(64));
	bTemplatesBuilt = true;
}

FString URuneRecognizer::ShapeToString(EShapeType Shape)
{
	switch (Shape)
	{
		case EShapeType::Circle:   return TEXT("Circle");
		case EShapeType::Line:     return TEXT("Line");
		case EShapeType::Triangle: return TEXT("Triangle");
		case EShapeType::VShape:   return TEXT("VShape");
		case EShapeType::Zigzag:   return TEXT("Zigzag");
		case EShapeType::Star:     return TEXT("Star");
		default:                   return TEXT("None");
	}
}

// ============================================================
//  Heuristic helpers
// ============================================================

void URuneRecognizer::NormalizeStroke(const TArray<FVector2D>& In, TArray<FVector2D>& Out, FVector2D& OutBBoxSize)
{
	Out.Reset(In.Num());
	if (In.Num() == 0)
	{
		OutBBoxSize = FVector2D::ZeroVector;
		return;
	}

	FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D Max(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());
	FVector2D Centroid(0, 0);
	for (const FVector2D& P : In)
	{
		Min.X = FMath::Min(Min.X, P.X);
		Min.Y = FMath::Min(Min.Y, P.Y);
		Max.X = FMath::Max(Max.X, P.X);
		Max.Y = FMath::Max(Max.Y, P.Y);
		Centroid += P;
	}
	Centroid /= static_cast<float>(In.Num());
	OutBBoxSize = Max - Min;

	const float Scale = FMath::Max(OutBBoxSize.X, OutBBoxSize.Y);
	const float InvScale = (Scale > KINDA_SMALL_NUMBER) ? 1.0f / Scale : 1.0f;
	for (const FVector2D& P : In)
	{
		Out.Add((P - Centroid) * InvScale);
	}
}

float URuneRecognizer::ComputePolygonArea(const TArray<FVector2D>& Stroke)
{
	if (Stroke.Num() < 3) return 0.0f;
	float Sum = 0.0f;
	const int32 N = Stroke.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Stroke[i];
		const FVector2D& B = Stroke[(i + 1) % N];
		Sum += A.X * B.Y - B.X * A.Y;
	}
	return FMath::Abs(Sum) * 0.5f;
}

float URuneRecognizer::ComputePerimeter(const TArray<FVector2D>& Stroke)
{
	if (Stroke.Num() < 2) return 0.0f;
	float Sum = 0.0f;
	for (int32 i = 1; i < Stroke.Num(); ++i)
	{
		Sum += FVector2D::Distance(Stroke[i - 1], Stroke[i]);
	}
	// closure for shape area calc
	Sum += FVector2D::Distance(Stroke.Last(), Stroke[0]);
	return Sum;
}

int32 URuneRecognizer::CountCurvaturePeaks(const TArray<FVector2D>& Stroke, float AngleThresholdDeg)
{
	if (Stroke.Num() < 5) return 0;

	// resample to fixed length first to make peak detection scale-invariant
	TArray<FVector2D> Resampled;
	Resample(Stroke, Resampled, 64);

	const float ThresholdRad = FMath::DegreesToRadians(AngleThresholdDeg);
	const int32 Window = 4;

	TArray<float> Angles;
	Angles.SetNum(Resampled.Num());
	for (int32 i = 0; i < Resampled.Num(); ++i)
	{
		const int32 Prev = (i - Window + Resampled.Num()) % Resampled.Num();
		const int32 Next = (i + Window) % Resampled.Num();
		const FVector2D V1 = (Resampled[i] - Resampled[Prev]).GetSafeNormal();
		const FVector2D V2 = (Resampled[Next] - Resampled[i]).GetSafeNormal();
		const float Dot = FVector2D::DotProduct(V1, V2);
		Angles[i] = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
	}

	// non-maximum suppression: count local maxima above threshold
	int32 Peaks = 0;
	const int32 N = Angles.Num();
	const int32 NMSWindow = 5;
	for (int32 i = 0; i < N; ++i)
	{
		if (Angles[i] < ThresholdRad) continue;
		bool bIsLocalMax = true;
		for (int32 j = 1; j <= NMSWindow; ++j)
		{
			const int32 L = (i - j + N) % N;
			const int32 R = (i + j) % N;
			if (Angles[L] >= Angles[i] || Angles[R] > Angles[i])
			{
				bIsLocalMax = false;
				break;
			}
		}
		if (bIsLocalMax) ++Peaks;
	}
	return Peaks;
}

// ============================================================
//  $1 Unistroke helpers
// ============================================================

void URuneRecognizer::Resample(const TArray<FVector2D>& In, TArray<FVector2D>& Out, int32 N)
{
	Out.Reset(N);
	if (In.Num() < 2 || N < 2)
	{
		Out = In;
		return;
	}

	// total path length
	float TotalLen = 0.0f;
	for (int32 i = 1; i < In.Num(); ++i)
	{
		TotalLen += FVector2D::Distance(In[i - 1], In[i]);
	}
	if (TotalLen < KINDA_SMALL_NUMBER)
	{
		for (int32 i = 0; i < N; ++i) Out.Add(In[0]);
		return;
	}

	const float Step = TotalLen / static_cast<float>(N - 1);
	float D = 0.0f;
	Out.Add(In[0]);
	for (int32 i = 1; i < In.Num(); ++i)
	{
		FVector2D A = In[i - 1];
		FVector2D B = In[i];
		float Seg = FVector2D::Distance(A, B);
		while (D + Seg >= Step && Out.Num() < N)
		{
			const float T = (Step - D) / Seg;
			const FVector2D Q = A + (B - A) * T;
			Out.Add(Q);
			A = Q;
			Seg = FVector2D::Distance(A, B);
			D = 0.0f;
		}
		D += Seg;
	}
	while (Out.Num() < N)
	{
		Out.Add(In.Last());
	}
}

float URuneRecognizer::Indicative_Angle(const TArray<FVector2D>& Stroke)
{
	if (Stroke.Num() < 2) return 0.0f;
	FVector2D Centroid(0, 0);
	for (const FVector2D& P : Stroke) Centroid += P;
	Centroid /= static_cast<float>(Stroke.Num());
	return FMath::Atan2(Centroid.Y - Stroke[0].Y, Centroid.X - Stroke[0].X);
}

void URuneRecognizer::RotateToZero(TArray<FVector2D>& Stroke)
{
	if (Stroke.Num() < 2) return;
	FVector2D Centroid(0, 0);
	for (const FVector2D& P : Stroke) Centroid += P;
	Centroid /= static_cast<float>(Stroke.Num());

	const float Angle = -Indicative_Angle(Stroke);
	const float CosA = FMath::Cos(Angle);
	const float SinA = FMath::Sin(Angle);
	for (FVector2D& P : Stroke)
	{
		const float DX = P.X - Centroid.X;
		const float DY = P.Y - Centroid.Y;
		P.X = DX * CosA - DY * SinA + Centroid.X;
		P.Y = DX * SinA + DY * CosA + Centroid.Y;
	}
}

void URuneRecognizer::ScaleToSquare(TArray<FVector2D>& Stroke, float Size)
{
	if (Stroke.Num() == 0) return;
	FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D Max(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());
	for (const FVector2D& P : Stroke)
	{
		Min.X = FMath::Min(Min.X, P.X); Min.Y = FMath::Min(Min.Y, P.Y);
		Max.X = FMath::Max(Max.X, P.X); Max.Y = FMath::Max(Max.Y, P.Y);
	}
	const FVector2D Box = Max - Min;
	const float SX = (Box.X > KINDA_SMALL_NUMBER) ? Size / Box.X : 1.0f;
	const float SY = (Box.Y > KINDA_SMALL_NUMBER) ? Size / Box.Y : 1.0f;
	for (FVector2D& P : Stroke)
	{
		P.X *= SX;
		P.Y *= SY;
	}
}

void URuneRecognizer::TranslateToOrigin(TArray<FVector2D>& Stroke)
{
	if (Stroke.Num() == 0) return;
	FVector2D Centroid(0, 0);
	for (const FVector2D& P : Stroke) Centroid += P;
	Centroid /= static_cast<float>(Stroke.Num());
	for (FVector2D& P : Stroke) P -= Centroid;
}

float URuneRecognizer::PathDistance(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
{
	const int32 N = FMath::Min(A.Num(), B.Num());
	if (N == 0) return TNumericLimits<float>::Max();
	float Sum = 0.0f;
	for (int32 i = 0; i < N; ++i)
	{
		Sum += FVector2D::Distance(A[i], B[i]);
	}
	return Sum / static_cast<float>(N);
}

float URuneRecognizer::DistanceAtBestAngle(const TArray<FVector2D>& Stroke, const TArray<FVector2D>& Tmpl)
{
	using namespace RuneRecMath;
	const float Range = FMath::DegreesToRadians(OneDollarAngleRange);
	const float Prec  = FMath::DegreesToRadians(OneDollarAnglePrecision);

	float A = -Range;
	float B = Range;

	auto DistanceAtAngle = [&](float Theta) -> float
	{
		TArray<FVector2D> Rot = Stroke;
		FVector2D Centroid(0, 0);
		for (const FVector2D& P : Rot) Centroid += P;
		Centroid /= static_cast<float>(Rot.Num());
		const float CosT = FMath::Cos(Theta);
		const float SinT = FMath::Sin(Theta);
		for (FVector2D& P : Rot)
		{
			const float DX = P.X - Centroid.X;
			const float DY = P.Y - Centroid.Y;
			P.X = DX * CosT - DY * SinT + Centroid.X;
			P.Y = DX * SinT + DY * CosT + Centroid.Y;
		}
		return PathDistance(Rot, Tmpl);
	};

	float X1 = GoldenRatioInv * A + (1.0f - GoldenRatioInv) * B;
	float F1 = DistanceAtAngle(X1);
	float X2 = (1.0f - GoldenRatioInv) * A + GoldenRatioInv * B;
	float F2 = DistanceAtAngle(X2);

	int32 Guard = 0;
	while (FMath::Abs(B - A) > Prec && Guard++ < 50)
	{
		if (F1 < F2)
		{
			B = X2;
			X2 = X1;
			F2 = F1;
			X1 = GoldenRatioInv * A + (1.0f - GoldenRatioInv) * B;
			F1 = DistanceAtAngle(X1);
		}
		else
		{
			A = X1;
			X1 = X2;
			F1 = F2;
			X2 = (1.0f - GoldenRatioInv) * A + GoldenRatioInv * B;
			F2 = DistanceAtAngle(X2);
		}
	}
	return FMath::Min(F1, F2);
}

// ============================================================
//  Templates
// ============================================================

void URuneRecognizer::BuildTemplate(EShapeType Shape, const TArray<FVector2D>& RawPoints)
{
	FOneDollarTemplate T;
	T.Shape = Shape;
	Resample(RawPoints, T.Points, 64);
	RotateToZero(T.Points);
	ScaleToSquare(T.Points, RuneRecMath::OneDollarSquareSize);
	TranslateToOrigin(T.Points);
	Templates.Add(MoveTemp(T));
}

TArray<FVector2D> URuneRecognizer::MakeCircleTemplate(int32 N)
{
	TArray<FVector2D> Out;
	Out.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(N - 1) * 2.0f * PI;
		Out.Add(FVector2D(FMath::Cos(T), FMath::Sin(T)));
	}
	return Out;
}

TArray<FVector2D> URuneRecognizer::MakeLineTemplate(int32 N)
{
	TArray<FVector2D> Out;
	Out.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(N - 1);
		Out.Add(FVector2D(-1.0f + 2.0f * T, 0.0f));
	}
	return Out;
}

TArray<FVector2D> URuneRecognizer::MakeTriangleTemplate(int32 N)
{
	const FVector2D V0(0.0f,  1.0f);
	const FVector2D V1(-1.0f, -0.5f);
	const FVector2D V2( 1.0f, -0.5f);
	TArray<FVector2D> Out;
	const int32 PerEdge = N / 3;
	auto Lerp2D = [](const FVector2D& A, const FVector2D& B, float T){ return A + (B - A) * T; };
	for (int32 i = 0; i < PerEdge; ++i) Out.Add(Lerp2D(V0, V1, static_cast<float>(i) / PerEdge));
	for (int32 i = 0; i < PerEdge; ++i) Out.Add(Lerp2D(V1, V2, static_cast<float>(i) / PerEdge));
	for (int32 i = 0; i < N - 2 * PerEdge; ++i) Out.Add(Lerp2D(V2, V0, static_cast<float>(i) / FMath::Max(1, N - 2 * PerEdge)));
	return Out;
}

TArray<FVector2D> URuneRecognizer::MakeVShapeTemplate(int32 N)
{
	const FVector2D V0(-1.0f,  1.0f);
	const FVector2D V1( 0.0f, -1.0f);
	const FVector2D V2( 1.0f,  1.0f);
	TArray<FVector2D> Out;
	const int32 Half = N / 2;
	auto Lerp2D = [](const FVector2D& A, const FVector2D& B, float T){ return A + (B - A) * T; };
	for (int32 i = 0; i < Half; ++i) Out.Add(Lerp2D(V0, V1, static_cast<float>(i) / Half));
	for (int32 i = 0; i < N - Half; ++i) Out.Add(Lerp2D(V1, V2, static_cast<float>(i) / FMath::Max(1, N - Half)));
	return Out;
}

TArray<FVector2D> URuneRecognizer::MakeZigzagTemplate(int32 N)
{
	// W shape: 4 peaks
	const TArray<FVector2D> Verts = {
		FVector2D(-1.0f,  0.5f),
		FVector2D(-0.6f, -0.5f),
		FVector2D(-0.2f,  0.5f),
		FVector2D( 0.2f, -0.5f),
		FVector2D( 0.6f,  0.5f),
		FVector2D( 1.0f, -0.5f),
	};
	TArray<FVector2D> Out;
	const int32 PerSeg = FMath::Max(1, N / (Verts.Num() - 1));
	auto Lerp2D = [](const FVector2D& A, const FVector2D& B, float T){ return A + (B - A) * T; };
	for (int32 s = 0; s < Verts.Num() - 1; ++s)
	{
		for (int32 i = 0; i < PerSeg && Out.Num() < N; ++i)
		{
			Out.Add(Lerp2D(Verts[s], Verts[s + 1], static_cast<float>(i) / PerSeg));
		}
	}
	while (Out.Num() < N) Out.Add(Verts.Last());
	return Out;
}

TArray<FVector2D> URuneRecognizer::MakeStarTemplate(int32 N)
{
	// 5-point star (one-stroke pentagram)
	const float OuterR = 1.0f;
	TArray<FVector2D> Verts;
	for (int32 i = 0; i < 5; ++i)
	{
		const float T = (-PI / 2.0f) + static_cast<float>(i) * (4.0f * PI / 5.0f); // skip 2 for pentagram
		Verts.Add(FVector2D(OuterR * FMath::Cos(T), OuterR * FMath::Sin(T)));
	}
	// Copy first vertex to a local before Add — passing Verts[0] directly trips
	// TArray::CheckAddress (Array.h:1957) because the element lives inside the
	// array buffer that Add may reallocate.
	const FVector2D ClosingVert = Verts[0];
	Verts.Add(ClosingVert);

	TArray<FVector2D> Out;
	const int32 PerSeg = FMath::Max(1, N / (Verts.Num() - 1));
	auto Lerp2D = [](const FVector2D& A, const FVector2D& B, float T){ return A + (B - A) * T; };
	for (int32 s = 0; s < Verts.Num() - 1; ++s)
	{
		for (int32 i = 0; i < PerSeg && Out.Num() < N; ++i)
		{
			Out.Add(Lerp2D(Verts[s], Verts[s + 1], static_cast<float>(i) / PerSeg));
		}
	}
	while (Out.Num() < N) Out.Add(Verts.Last());
	return Out;
}
