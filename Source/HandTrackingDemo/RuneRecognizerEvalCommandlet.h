#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RuneRecognizerEvalCommandlet.generated.h"

/**
 * 룬 인식기 평가 commandlet.
 *
 * 사용법:
 *   UnrealEditor-Cmd.exe HandTrackingDemo.uproject -run=RuneRecognizerEval -log
 *
 * 입력: Content/Eval/RuneEvalSet.json (라벨드 stroke 30개)
 * 출력: Saved/Logs/HandTrackingDemo.log 의 [EVAL] marker 라인
 *   [EVAL] Circle.precision=X.X Line.precision=X.X ... macro_F1=X.X
 */
UCLASS()
class HANDTRACKINGDEMO_API URuneRecognizerEvalCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URuneRecognizerEvalCommandlet();

	virtual int32 Main(const FString& Params) override;
};
