# HandTrackingDemo — 룬 드로잉 마법 시스템

UE 5.7.4 / Meta Quest 3 / C++

핀치(엄지+가운데 손가락)로 공중에 도형을 그리면 도형 종류에 따라 다른 마법이 발동되는 핸드트래킹 데모. 양손 독립 작동.

---

## 1. 사용법

1. Quest 3 헤드셋을 끼고 앱 실행
2. 양손 손끝에 작은 회색 indicator(원)이 떠야 함 — 핸드트래킹 + 본 매칭이 살아있다는 신호
3. **엄지 + 가운데 손가락**을 붙임 → indicator가 큰 시안색(좌)/마젠타색(우)으로 변함 = Drawing 시작
4. 그 자세 유지하고 공중에 도형 그림 → 트레일이 손끝을 따라옴
5. 손가락 떼면 0.4초 후 인식 → 화면에 `[L] Circle conf=0.85` 4초간 표시 + 마법 비주얼

> 트리거에 검지 대신 **가운데 손가락**을 쓰는 이유: Quest 3은 엄지+검지 핀치를 시스템 메뉴 제스처로 가로챔. 가운데 손가락 핀치는 시스템 제스처가 없어서 자유롭게 사용 가능.

### 6종 도형 → 마법

| 도형 | 그리는 법 | 마법 | 디버그 비주얼 |
|---|---|---|---|
| 원 (Circle) | 동그랗게 한 바퀴 | 푸른 방어막 5초 | Sphere |
| 직선 (Line) | 길게 한 줄 | 광선 | Line |
| 삼각형 (Triangle) | △ 꼭짓점 3개 | 1.5m 앞 룬 서클 + 소환물 | Circle + Sphere |
| V자 (V) | ∨ 한 번 꺾기 | 분할 공격 | Line × 2 |
| 지그재그 (Zigzag) | W처럼 4-5번 꺾기 | 연쇄 번개 | Line × 4-5 |
| 별 (Star) | ★ 펜타그램 한 획 | 폭발 발사체 | Line + Sphere |

---

## 2. 동작 원리

### 2.1 컴포넌트 트리

```
AHandPawn
├─ VRCamera (HMD lock)
├─ LeftMC, RightMC                — MotionControllerComponent (Left/Right)
├─ LeftHand, RightHand             — UOculusXRHandComponent (MC 자식, 본 트랜스폼만 갱신)
├─ LeftRuneDrawing, RightRuneDrawing — URuneDrawingComponent (per-hand 핀치+샘플링+FSM)
└─ SpellComponent                  — URuneSpellComponent (양손 broadcast 수신, 6종 BIE 디스패치)
```

`HandPawn::BeginPlay`는 `SetTrackingOrigin(LocalFloor)`와 `SetSimultaneousHandsAndControllersEnabled(true)`를 호출 — 컨트롤러를 들어도 손 mesh가 동시에 추적됨.

### 2.2 단계별 파이프라인

```
손가락 본 위치 추적  →  핀치 감지  →  60Hz 샘플링  →  HMD 평면 정사영  →  모양 인식  →  마법 디스패치
```

#### (1) 본 위치 추적 — `URuneDrawingComponent::TryGetIndexTipWorld`

OculusXR가 매 프레임 `UOculusXRHandComponent`(UPoseableMeshComponent 상속)의 본 트랜스폼을 갱신한다. `EOculusXRBone::Index_Tip` 등 enum을 통해 본 이름(`"Index_Tip"`, `"Middle_Tip"`, `"Thumb_Tip"`)을 resolve해서 월드 좌표를 뽑는다.

> ⚠️ 주의: `UOculusXRHandComponent`의 SkeletalMesh는 **OpenXR session 시작 후에야** 만들어진다. BeginPlay 시점엔 NumBones=0이라 본 매칭이 실패하고, GetComponentLocation으로 폴백된다. 이걸 모르면 양손 wrist 거리가 0으로 측정되어 영구 핀치 상태에 빠짐. 그래서 매 틱에서 본 매칭을 시도하고, 본 매칭 실패 시 strength=0으로 fallback해서 false-pinch를 막는다.

#### (2) 핀치 감지 — `TryGetPinchStrength`

엄지끝 본 vs 가운데 손가락끝 본 거리로 0~1 strength를 계산:

```
strength = clamp(1 - (Distance - 2cm) / 7cm, 0, 1)

  2cm 이하  → 1.0  (확실한 핀치)
  6cm       → 0.43
  9cm 이상  → 0.0  (떨어짐)
```

여기에 **3중 안전망**을 씌워서 안정적인 detection:

1. **Schmitt trigger 히스테리시스** — 시작 0.7, 종료 0.2 임계값. 0.5 근처에서 떨려도 상태 안 바뀜
2. **Release debounce 200ms** — strength가 임계값 밑으로 떨어져도 200ms 동안 연속 떨어져 있어야 진짜 release. 한두 프레임 jitter는 무시
3. **본 매칭 실패 시 false 반환** — wrist 폴백 거리=0으로 인한 영구 핀치 방지

#### (3) FSM (Finite State Machine)

```
        pinch start (strength > 0.7)
Idle ──────────────────────────────→ Drawing
  ↑                                     │
  │ no re-pinch within 400ms            │ pinch release (strength < 0.2 for 200ms)
  │ continuity window                   │
  │                                     ↓
  └──── EndSessionAndRecognize ←──── ReleaseWait (400ms)
                                          │
                                          │ re-pinch within 400ms
                                          ↓
                                      Drawing (continued, 같은 stroke 이어붙임)
```

- **Continuity window 400ms**: 사람이 도형 중간에 잠깐 핀치가 풀리는 미세 떨림을 같은 stroke로 보존 (별 같은 다중 획 도형에 자연스러움)
- **Hard cap 4.0s**: 핀치 계속 누르고 있어도 4초 지나면 강제 인식 (무한 누적 방지)

#### (4) 60Hz 샘플링 + HMD 평면 정사영

`Drawing` 상태에서 매 틱:
- `SampleAccumulator += DeltaTime` 누적, 1/60s 차면 검지 끝 위치 1개 push, accumulator 차감
- 같은 점을 HMD 평면에 투영해서 2D 좌표도 push

**왜 평면 투영이 필요한가**: 손은 3D 공간에서 움직이지만, 모양 인식 알고리즘은 2D를 가정한다. "원"을 그리려는 사람은 자기 시야 평면에 원을 그리는 거지 XY 월드 평면에 그리는 게 아니다. 핀치 시작 순간의 HMD forward를 평면 normal로 잡아서 정사영:

```
PlaneOrigin  = CameraLocation + CameraForward * 30cm
PlaneRight   = CameraRight (camera Y축)
PlaneUp      = PlaneRight × CameraForward
PlaneNormal  = CameraForward

// 매 샘플:
Local = WorldPoint - PlaneOrigin
U = dot(Local, PlaneRight)    ← 화면 가로
V = dot(Local, PlaneUp)        ← 화면 세로
Stroke2D.push({U, V})
```

평면 normal 각도 체크: stroke 평균 법선이 HMD forward와 60도 이상 어긋나면 거부 (사용자가 옆면에 그린 케이스). 단, 30개 미만 sample은 normal 추정이 노이즈라 스킵.

#### (5) 모양 인식 — `URuneRecognizer::Recognize`

**2단계 인식기**: 휴리스틱 우선 → confidence 부족하면 $1 Unistroke fallback → dual confidence 결합.

##### 휴리스틱 (빠르고 specific)

```
1. NormalizeStroke: 중심 0, bbox 스케일 정규화
2. 측정값 3개:
   - Circularity = 4πA / P²              (1.0=완벽한 원, 0=직선)
   - AspectRatio = max축 / min축
   - PeakCount   = 곡률 피크 (sharp turn) 개수

3. 우선순위 분기:
   Circularity > 0.75                     → Circle  (conf = Circularity)
   AspectRatio > 8 AND PeakCount ≤ 1      → Line
   PeakCount = 1 AND AspectRatio < 8      → V       (한 번 꺾임)
   PeakCount = 3                          → Triangle
   PeakCount = 5 AND BBoxRatio > 0.6      → Star
   PeakCount = 4~6 AND AspectRatio > 1.5  → Zigzag
   else                                   → None
```

곡률 피크는 stroke를 64점으로 리샘플 후, 각 점에서 양옆 4개 점이 이루는 각도가 50도 임계값 넘으면 peak. NMS 5점 윈도우로 인접 피크 합침.

##### $1 Unistroke Recognizer (Wobbrock 2007)

```
1. Resample            : stroke를 정확히 64점으로 균등 간격 보간
2. RotateToZero        : indicative angle 0도로 회전 정렬 (rotation invariance)
3. ScaleToSquare       : 250×250 박스 채우기 (scale invariance)
4. TranslateToOrigin   : 중심 0,0
5. 6종 템플릿(원/직선/삼각형/V/지그재그/별)과 각각 비교:
   - DistanceAtBestAngle: ±45도 범위 황금비분할 탐색
   - PathDistance: 64점 평균 유클리드 거리
6. 최소 거리 템플릿 = 결정된 모양
   confidence = 1 - BestDist / HalfDiagonal
```

##### Dual confidence 결합

```
휴리스틱과 $1이 같은 모양  →  hmean (조화평균)
다른 모양 + |ΔConf| > 0.15 →  우세한 쪽 채택, conf -= 0.1
둘 다 < 0.5               →  None (확신 없음)
나머지                    →  $1 우선 (템플릿 매칭이 더 안정적)
```

#### (6) 마법 디스패치 — `URuneSpellComponent`

`URuneDrawingComponent::OnShapeRecognized` (양손 2개 델리게이트) → SpellComponent가 BeginPlay에서 양손 모두 바인드 → 인식되면 `Broadcast(Result)` → SpellComponent가 6종 BIE + DebugDraw* sibling 함수 호출.

```cpp
switch (R.Shape) {
    case Circle:
        OnCastCircleShield(Center, 50.0f, 5.0f);   // BlueprintImplementableEvent
        DebugDrawCircleShield(Center, 50.0f, 5.0f); // 항상 호출 (BP override 없어도 보임)
    ...
}
```

`BlueprintImplementableEvent`이라 나중에 BP에서 진짜 Niagara 파티클로 override 가능. 지금은 DrawDebugSphere/Line으로 placeholder.

---

## 3. 빌드 + 배포

### 사전 요구사항

| 도구 | 버전 |
|---|---|
| Unreal Engine | 5.7.4 |
| Microsoft OpenJDK | 21 |
| Android SDK | 34 (with NDK 27.2.12479018) |
| Quest 3 | OS 60+ (USB 디버깅 허용) |

### 한 줄 빌드+배포

`BuildAndDeployQuest3.cmd` 실행 (JAVA_HOME / ANDROID_HOME / DEVICE 본인 환경에 맞게 수정):

```bat
set "JAVA_HOME=C:\Program Files\Microsoft\jdk-21.0.10.7-hotspot"
set "ANDROID_HOME=C:\Users\YOU\AppData\Local\Android\Sdk"
set "NDKROOT=%ANDROID_HOME%\ndk\27.2.12479018"
set "DEVICE=YOUR_QUEST_SERIAL"  REM `adb devices`로 확인

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" ^
  BuildCookRun -project="%PROJECT%" -platform=Android -clientconfig=Development ^
  -cookflavor=ASTC -build -cook -stage -package -pak -deploy -device=%DEVICE%
```

UAT가 컴파일 → cook → APK 패키징 → adb install 자동 처리. 핫 캐시 기준 3-4분.

### Quest 3가 자꾸 sleep 들어갈 때

테스트 중 헤드셋 proximity sensor가 자꾸 잠재울 때:

```bat
adb shell svc power stayon usb
adb shell am broadcast -a com.oculus.vrpowermanager.automation_disable
adb shell am broadcast -a com.oculus.vrpowermanager.prox_close
```

---

## 4. 디버깅 콘솔 변수

`키 → 콘솔 → 다음 변수 토글:

| 변수 | 기본 | 효과 |
|---|---|---|
| `r.RuneDraw.UseHeuristic 0` | 1 | 휴리스틱 끄고 $1만 사용 |
| `r.RuneDraw.UseFallback 0` | 1 | $1 끄고 휴리스틱만 |
| `r.RuneDraw.PlaneCheck 0` | 1 | 60도 평면 체크 끔 (옆에서 그려도 인식) |
| `r.RuneDraw.Continuity 0` | 1 | 400ms 연결 끔 (즉시 인식) |
| `r.RuneDraw.PlanarSnap 0` | 1 | 평면 투영 끄고 raw 3D X/Z 사용 |
| `r.RuneDraw.DisableStar 1` | 0 | 별 후보 제외 (별 정확도 낮을 때 escape hatch) |

5중 의존성을 isolate해서 어디가 망가졌는지 디버깅 가능.

---

## 5. 평가 commandlet

자동 정확도 평가 (`Content/Eval/RuneEvalSet.json` 30 샘플 대상):

```bat
"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  HandTrackingDemo.uproject -run=RuneRecognizerEval -log
```

→ `Saved/Logs/HandTrackingDemo.log`에 confusion matrix + macro F1 출력:

```
[EVAL] Circle.precision=0.92 Circle.recall=0.85 ...
[EVAL] macro_F1=0.78
```

---

## 6. 프로젝트 구조

```
Source/HandTrackingDemo/
├─ HandPawn.h/.cpp               — VR Pawn (양손 + Rune + Spell 컴포넌트 자동 부착)
├─ HandGameMode.h/.cpp           — DefaultPawnClass = AHandPawn
├─ RuneTypes.h                   — EShapeType / EHandSide / FShapeRecognitionResult / FOnShapeRecognized 델리게이트
├─ RuneDrawingComponent.h/.cpp   — per-hand FSM + 60Hz 샘플링 + 평면 정사영 + 트레일 디버그
├─ RuneRecognizer.h/.cpp         — UObject. 휴리스틱 + $1 + dual confidence
├─ RuneSpellComponent.h/.cpp     — per-pawn. 6종 BIE + DebugDraw 디스패치
├─ RuneRecognizerEvalCommandlet.h/.cpp — 자동 평가 (UCommandlet)
└─ HandTrackingDemo.Build.cs     — Json/JsonUtilities 추가

Content/Eval/RuneEvalSet.json    — 30개 라벨 stroke (도형당 5개)
SETUP.md                         — 빠른 셋업 가이드
BuildAndDeployQuest3.cmd         — 원클릭 빌드+배포
```

---

## 7. 알려진 이슈 / 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| 손끝 indicator가 안 보임 | 핸드트래킹 미허용. Quest 설정 → 손과 컨트롤러 → 핸드트래킹 ON |
| 핀치해도 상태가 안 변함 | 본 이름 매칭 실패. Output Log에서 `[BONES]` 로그로 실제 본 이름 확인 |
| 자꾸 끊겨서 짧은 stroke로 잘림 | 핀치 jitter — 가운데 손가락을 더 단단히 붙이거나 PinchEndThreshold 더 낮추기 |
| 별만 정확도 낮음 | 사람마다 별 그리는 순서가 달라서. `r.RuneDraw.DisableStar 1`로 우회 |
| 시스템 메뉴가 자꾸 뜸 | 엄지+검지로 핀치하고 있음. 엄지+**가운데 손가락** 사용 |
| APK 설치 후 즉시 종료 | 1차 배포 시 발견된 버그 (TArray::Add 자기 참조). 이미 수정됨 |
