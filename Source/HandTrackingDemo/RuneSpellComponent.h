#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RuneTypes.h"
#include "RuneSpellComponent.generated.h"

class URuneDrawingComponent;

/**
 * 마법 디스패처 (per-pawn).
 *
 * BP_HandPawn에 1개 부착. BeginPlay 시 owner의 모든 URuneDrawingComponent를 찾아
 * OnShapeRecognized 델리게이트에 바인딩. 도형 인식 결과를 6종 BIE로 분기.
 *
 * 각 BIE 옆에 DebugDraw* 가 항상 호출됨 — BP override 여부와 무관하게 PIE에서 시각 확보.
 */
UCLASS(ClassGroup = (Rune), meta = (BlueprintSpawnableComponent), Blueprintable)
class HANDTRACKINGDEMO_API URuneSpellComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URuneSpellComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Spell|Tuning")
	float CircleShieldRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Spell|Tuning")
	float CircleShieldDurationSec = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Spell|Tuning")
	float LineRayLength = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Spell|Tuning")
	float TriangleSummonForwardDist = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Spell|Tuning")
	float StarProjectileLength = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rune|Spell|Debug")
	bool bAlwaysDrawDebug = true;

	// --- BIE: BP에서 override 가능. 비주얼 에셋(Niagara/Particle/Mesh) 차후 BP 바인딩. ---

	UFUNCTION(BlueprintImplementableEvent, Category = "Rune|Spell|Cast")
	void OnCastCircleShield(FVector Center, float Radius, float DurationSec, EHandSide Side);

	UFUNCTION(BlueprintImplementableEvent, Category = "Rune|Spell|Cast")
	void OnCastLineRay(FVector Origin, FVector Direction, float Length, EHandSide Side);

	UFUNCTION(BlueprintImplementableEvent, Category = "Rune|Spell|Cast")
	void OnCastTriangleSummon(FVector RuneCenter, EHandSide Side);

	UFUNCTION(BlueprintImplementableEvent, Category = "Rune|Spell|Cast")
	void OnCastVShape(FVector Origin, FVector LeftDir, FVector RightDir, EHandSide Side);

	UFUNCTION(BlueprintImplementableEvent, Category = "Rune|Spell|Cast")
	void OnCastZigzagChain(FVector Origin, FVector Direction, EHandSide Side);

	UFUNCTION(BlueprintImplementableEvent, Category = "Rune|Spell|Cast")
	void OnCastStarProjectile(FVector Origin, FVector Direction, EHandSide Side);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<URuneDrawingComponent>> BoundDrawingComponents;

	UFUNCTION()
	void HandleShapeRecognized(const FShapeRecognitionResult& Result);

	void DispatchSpell(const FShapeRecognitionResult& R);

	void DebugDrawCircleShield(const FVector& Center, float Radius, float DurationSec, EHandSide Side) const;
	void DebugDrawLineRay(const FVector& Origin, const FVector& Direction, float Length, EHandSide Side) const;
	void DebugDrawTriangleSummon(const FVector& RuneCenter, EHandSide Side) const;
	void DebugDrawVShape(const FVector& Origin, const FVector& LeftDir, const FVector& RightDir, EHandSide Side) const;
	void DebugDrawZigzagChain(const FVector& Origin, const FVector& Direction, EHandSide Side) const;
	void DebugDrawStarProjectile(const FVector& Origin, const FVector& Direction, EHandSide Side) const;
};
