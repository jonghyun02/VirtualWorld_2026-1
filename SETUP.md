# HandTrackingDemo - 실행 가이드

UE 5.7.4 / Quest 3 / Meta XR Simulator.

룬 드로잉 마법 시스템이 **C++ HandPawn에 직접 내장**되어 있어 BP 셋업 없이 바로 PIE 누르면 됩니다.

## 1. 빌드

1. 프로젝트 루트에서 `HandTrackingDemo.uproject` 우클릭 → "Generate Visual Studio project files"
2. `HandTrackingDemo.sln` 열고 Win64 / Development Editor 빌드 (Ctrl+B)
3. 에디터 자동 실행

## 2. Meta XR Simulator 활성화 (PIE 핸드 시뮬용)

1. Edit → Plugins → "Meta XR Simulator" 검색 → Enable
2. 에디터 재시작
3. Toolbar의 PIE 옆 드롭다운에서 **Meta XR Simulator** 선택

## 3. 실행

1. 그냥 PIE 재생 ▶ 누름
2. Simulator 창에서 양손 핸드트래킹 모드 켜기 (창 안에 단축키 안내 있음)
3. 핀치(엄지+검지 접촉)로 공중에 도형 그리기 → 핀치 떼면 인식 → 마법 발동

## 4. 6종 도형 → 마법

| 도형 | 마법 | 디버그 비주얼 |
|---|---|---|
| 원 (Circle) | 푸른 방어막 5초 | Sphere |
| 직선 (Line) | 광선 | Line |
| 삼각형 (Triangle) | 룬 서클 + 소환 | Circle + Sphere |
| V자 (VShape) | 분할 공격 (양 갈래) | 2개 Line |
| 지그재그 (Zigzag) | 연쇄 번개 | 4-5개 Line |
| 별 (Star) | 폭발 발사체 | Line + Sphere |

## 5. 첫 PIE 시 Output Log 확인

성공 로그:
```
[INIT] Hand=L HandRef=LeftHand
[INIT] Hand=R HandRef=RightHand
[INIT] Bound 2 drawing components
```

도형 그리기 후:
```
[FSM] Hand=L Idle->Drawing plane R.U=0.0001
[CAST] Hand=L label=Circle confidence=0.892 samples=58
[SPELL] Hand=L shape=Circle confidence=0.89
```

## 6. Console Variables (디버깅, 선택)

` 키로 콘솔 열고 입력:

| 변수 | 기본 | 설명 |
|---|---|---|
| `r.RuneDraw.UseHeuristic` | 1 | 휴리스틱 인식기 토글 |
| `r.RuneDraw.UseFallback` | 1 | $1 Unistroke fallback 토글 |
| `r.RuneDraw.PlaneCheck` | 1 | 평면 normal 60도 체크 토글 |
| `r.RuneDraw.Continuity` | 1 | 핀치 release 400ms continuity 토글 |
| `r.RuneDraw.PlanarSnap` | 1 | stroke 평면 정사영 토글 |
| `r.RuneDraw.DisableStar` | 0 | 별 도형 후보 제외 (escape hatch) |

## 7. 자동 평가 commandlet (선택)

```
"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\projects\UnrealEngine\VirtualWorld_2026-1\HandTrackingDemo.uproject" ^
  -run=RuneRecognizerEval -log
```

→ `Saved/Logs/HandTrackingDemo.log`에 `[EVAL] Circle.precision=X.X ... macro_F1=X.X` 출력.

## 8. 트러블슈팅

- **PIE에서 손이 안 보임**: Meta XR Simulator 플러그인 활성화 확인 + Simulator 모드로 PIE 시작했는지 확인
- **빌드 에러 OculusXRHandTracking 모듈 없음**: 보통 `OculusXRInput`만 있어도 `EOculusXRBone` 사용 가능. 그래도 에러 나면 `Build.cs`의 `PublicDependencyModuleNames`에 `"OculusXRHandTracking"` 추가
- **본 이름 매칭 실패** (`HandRef=<none>` 로그): `Hand_IndexTip` / `XRHand_IndexTip` / `hand_index_3` / `Hand_Index3` 4개 후보 중 매칭 안 되면 OculusXR 플러그인 버전이 다른 본 이름 사용. Output Log의 본 이름 dump 확인 후 `RuneDrawingComponent.cpp:CandidateBoneNames`에 추가
- **별 인식 정확도 낮음**: 콘솔에 `r.RuneDraw.DisableStar 1` → 5종(원/직선/삼각형/V자/지그재그)으로 자동 폴백
