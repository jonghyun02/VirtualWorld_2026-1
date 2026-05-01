#include "RuneRecognizerEvalCommandlet.h"
#include "RuneRecognizer.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuneEval, Log, All);

URuneRecognizerEvalCommandlet::URuneRecognizerEvalCommandlet()
{
	IsClient = false;
	IsEditor = false;
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	struct FEvalSample
	{
		EShapeType Label;
		TArray<FVector2D> Points;
	};

	EShapeType ShapeFromString(const FString& S)
	{
		if (S.Equals(TEXT("Circle"),   ESearchCase::IgnoreCase)) return EShapeType::Circle;
		if (S.Equals(TEXT("Line"),     ESearchCase::IgnoreCase)) return EShapeType::Line;
		if (S.Equals(TEXT("Triangle"), ESearchCase::IgnoreCase)) return EShapeType::Triangle;
		if (S.Equals(TEXT("VShape"),   ESearchCase::IgnoreCase)) return EShapeType::VShape;
		if (S.Equals(TEXT("Zigzag"),   ESearchCase::IgnoreCase)) return EShapeType::Zigzag;
		if (S.Equals(TEXT("Star"),     ESearchCase::IgnoreCase)) return EShapeType::Star;
		return EShapeType::None;
	}

	bool LoadEvalSet(const FString& Path, TArray<FEvalSample>& OutSamples)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			UE_LOG(LogRuneEval, Error, TEXT("Failed to load eval set: %s"), *Path);
			return false;
		}

		TSharedPtr<FJsonValue> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogRuneEval, Error, TEXT("Failed to parse JSON: %s"), *Path);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Root->TryGetArray(Arr) || !Arr) return false;

		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!V->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid()) continue;
			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

			FString ShapeStr;
			if (!Obj->TryGetStringField(TEXT("shape"), ShapeStr)) continue;

			const TArray<TSharedPtr<FJsonValue>>* PtsArr = nullptr;
			if (!Obj->TryGetArrayField(TEXT("points"), PtsArr)) continue;

			FEvalSample Sample;
			Sample.Label = ShapeFromString(ShapeStr);
			for (const TSharedPtr<FJsonValue>& Pt : *PtsArr)
			{
				const TArray<TSharedPtr<FJsonValue>>* Coord = nullptr;
				if (!Pt->TryGetArray(Coord) || Coord->Num() < 2) continue;
				const float X = static_cast<float>((*Coord)[0]->AsNumber());
				const float Y = static_cast<float>((*Coord)[1]->AsNumber());
				Sample.Points.Add(FVector2D(X, Y));
			}
			if (Sample.Points.Num() >= 8 && Sample.Label != EShapeType::None)
			{
				OutSamples.Add(MoveTemp(Sample));
			}
		}
		return OutSamples.Num() > 0;
	}
}

int32 URuneRecognizerEvalCommandlet::Main(const FString& Params)
{
	URuneRecognizer::EnsureTemplatesBuilt();

	const FString EvalPath = FPaths::ProjectContentDir() / TEXT("Eval/RuneEvalSet.json");
	TArray<FEvalSample> Samples;
	if (!LoadEvalSet(EvalPath, Samples))
	{
		UE_LOG(LogRuneEval, Error, TEXT("[EVAL] Aborting: no usable samples in %s"), *EvalPath);
		return 1;
	}

	// confusion matrix [actual][predicted], indexed by EShapeType
	constexpr int32 NumShapes = 7; // None + 6
	int32 CM[NumShapes][NumShapes] = { { 0 } };

	for (const FEvalSample& S : Samples)
	{
		float Conf = 0.0f;
		const EShapeType Pred = URuneRecognizer::Recognize(S.Points, Conf, true, true, false);
		const int32 Actual = static_cast<int32>(S.Label);
		const int32 Predicted = static_cast<int32>(Pred);
		CM[Actual][Predicted]++;
		UE_LOG(LogRuneEval, Verbose, TEXT("  sample actual=%s predicted=%s conf=%.2f"),
			*URuneRecognizer::ShapeToString(S.Label),
			*URuneRecognizer::ShapeToString(Pred),
			Conf);
	}

	// per-class precision/recall/f1
	TArray<float> F1s;
	FString Line = TEXT("[EVAL]");
	for (int32 c = 1; c < NumShapes; ++c)
	{
		int32 TP = CM[c][c];
		int32 FP = 0;
		int32 FN = 0;
		for (int32 r = 0; r < NumShapes; ++r)
		{
			if (r != c) FP += CM[r][c];
			if (r != c) FN += CM[c][r];
		}
		const float P = (TP + FP > 0) ? static_cast<float>(TP) / (TP + FP) : 0.0f;
		const float R = (TP + FN > 0) ? static_cast<float>(TP) / (TP + FN) : 0.0f;
		const float F1 = (P + R > 0) ? 2.0f * P * R / (P + R) : 0.0f;
		F1s.Add(F1);

		Line += FString::Printf(TEXT(" %s.precision=%.2f"),
			*URuneRecognizer::ShapeToString(static_cast<EShapeType>(c)), P);
	}

	float MacroF1 = 0.0f;
	for (float F : F1s) MacroF1 += F;
	MacroF1 /= FMath::Max(1, F1s.Num());
	Line += FString::Printf(TEXT(" macro_F1=%.2f"), MacroF1);

	UE_LOG(LogRuneEval, Display, TEXT("%s"), *Line);
	UE_LOG(LogRuneEval, Display, TEXT("[EVAL] Total samples=%d MacroF1=%.3f"), Samples.Num(), MacroF1);

	return 0;
}
