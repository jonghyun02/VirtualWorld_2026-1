#include "HandPawn.h"
#include "Camera/CameraComponent.h"
#include "MotionControllerComponent.h"
#include "Components/SceneComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "OculusXRHandComponent.h"
#include "OculusXRInputFunctionLibrary.h"

AHandPawn::AHandPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// VR 카메라 — HMD 에 자동 락
	VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
	VRCamera->SetupAttachment(SceneRoot);
	VRCamera->bLockToHmd = true;

	// 모션 컨트롤러 — Quest 2 Touch / Quest 3 Touch Plus 모두 호환
	LeftMC = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftMC"));
	LeftMC->SetupAttachment(SceneRoot);
	LeftMC->SetTrackingMotionSource(FName(TEXT("Left")));

	RightMC = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightMC"));
	RightMC->SetupAttachment(SceneRoot);
	RightMC->SetTrackingMotionSource(FName(TEXT("Right")));

	// 시스템 핸드 메쉬 — MotionController 자식으로 attach.
	// UOculusXRHandComponent 는 PoseableMeshComponent 의 본 트랜스폼만 갱신
	// (손가락 관절). 컴포넌트 자체의 월드 위치는 부모가 결정함. MC 에 attach
	// 해야 손 위치 추적이 됨. Meta Quest OpenXR 런타임은 컨트롤러를 내려놓아도
	// MC 에 손 wrist pose 를 동일 motion source 로 계속 보내주므로, 컨트롤러
	// 없이 손만 있어도 위치 추적이 작동. ConfidenceBehavior=None 으로 신뢰도
	// 낮은 프레임에서도 메쉬 숨기지 않음.
	LeftHand = CreateDefaultSubobject<UOculusXRHandComponent>(TEXT("LeftHand"));
	LeftHand->SetupAttachment(LeftMC);
	LeftHand->SkeletonType       = EOculusXRHandType::HandLeft;
	LeftHand->MeshType           = EOculusXRHandType::HandLeft;
	LeftHand->ConfidenceBehavior = EOculusXRConfidenceBehavior::None;

	RightHand = CreateDefaultSubobject<UOculusXRHandComponent>(TEXT("RightHand"));
	RightHand->SetupAttachment(RightMC);
	RightHand->SkeletonType       = EOculusXRHandType::HandRight;
	RightHand->MeshType           = EOculusXRHandType::HandRight;
	RightHand->ConfidenceBehavior = EOculusXRConfidenceBehavior::None;

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AHandPawn::BeginPlay()
{
	Super::BeginPlay();

	// VR 트래킹 원점을 바닥 기준으로 설정 (서서/앉아서 둘 다 OK)
	UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::LocalFloor);

	// 컨트롤러 + 손 동시 추적 활성화 (Quest 3 의 XR_META_simultaneous_hands_and_controllers).
	// 기본은 OFF — 컨트롤러 잡으면 손 트래킹 자동 중단됨. 이걸 켜야 컨트롤러 잡고
	// 있는 동안에도 손 메쉬가 보이고 손가락 관절이 추적됨.
	UOculusXRInputFunctionLibrary::SetSimultaneousHandsAndControllersEnabled(true);
}
