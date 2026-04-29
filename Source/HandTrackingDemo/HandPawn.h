#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HandPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class USceneComponent;
class UOculusXRHandComponent;

/**
 * Quest 3 Hand Tracking Demo Pawn.
 *
 * 컨트롤러 + 시스템 핸드 메쉬가 자동 전환됨.
 *  - 컨트롤러를 들고 있으면: MotionController 가 위치 추적, 손 메쉬는 자동 숨김
 *  - 컨트롤러를 내려놓으면: OculusXRHandComponent 가 핸드 트래킹으로 전환되어
 *    Quest 3 가 보여주는 시스템 손 메쉬가 그대로 보임
 *
 * 모든 컴포넌트는 C++ 생성자에서 만들어지며 블루프린트 자산이 필요 없음.
 * 그냥 Place Actors 패널이나 GameMode 의 DefaultPawnClass 로 지정해서 사용.
 */
UCLASS()
class HANDTRACKINGDEMO_API AHandPawn : public APawn
{
	GENERATED_BODY()

public:
	AHandPawn();

protected:
	virtual void BeginPlay() override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	UCameraComponent* VRCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	UMotionControllerComponent* LeftMC;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	UMotionControllerComponent* RightMC;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|HandTracking")
	UOculusXRHandComponent* LeftHand;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|HandTracking")
	UOculusXRHandComponent* RightHand;
};
