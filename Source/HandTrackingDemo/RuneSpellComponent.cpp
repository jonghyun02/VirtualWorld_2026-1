#include "RuneSpellComponent.h"
#include "RuneDrawingComponent.h"
#include "RuneRecognizer.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogRuneSpell, Log, All);

URuneSpellComponent::URuneSpellComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void URuneSpellComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	TArray<URuneDrawingComponent*> Drawers;
	Owner->GetComponents<URuneDrawingComponent>(Drawers);
	for (URuneDrawingComponent* D : Drawers)
	{
		if (!D) continue;
		D->OnShapeRecognized.AddDynamic(this, &URuneSpellComponent::HandleShapeRecognized);
		BoundDrawingComponents.Add(D);
	}
	UE_LOG(LogRuneSpell, Log, TEXT("[INIT] Bound %d drawing components"), BoundDrawingComponents.Num());
}

void URuneSpellComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (URuneDrawingComponent* D : BoundDrawingComponents)
	{
		if (D)
		{
			D->OnShapeRecognized.RemoveDynamic(this, &URuneSpellComponent::HandleShapeRecognized);
		}
	}
	BoundDrawingComponents.Reset();
	Super::EndPlay(EndPlayReason);
}

void URuneSpellComponent::HandleShapeRecognized(const FShapeRecognitionResult& Result)
{
	UE_LOG(LogRuneSpell, Log, TEXT("[SPELL] Hand=%s shape=%s confidence=%.2f"),
		Result.HandSide == EHandSide::Left ? TEXT("L") : TEXT("R"),
		*URuneRecognizer::ShapeToString(Result.Shape),
		Result.Confidence);

	DispatchSpell(Result);
}

void URuneSpellComponent::DispatchSpell(const FShapeRecognitionResult& R)
{
	const FVector Origin = R.OriginWorld;
	const FVector Forward = R.ForwardWorld.GetSafeNormal();

	switch (R.Shape)
	{
		case EShapeType::Circle:
		{
			const FVector Center = Origin + Forward * 60.0f;
			OnCastCircleShield(Center, CircleShieldRadius, CircleShieldDurationSec, R.HandSide);
			if (bAlwaysDrawDebug) DebugDrawCircleShield(Center, CircleShieldRadius, CircleShieldDurationSec, R.HandSide);
			break;
		}
		case EShapeType::Line:
		{
			OnCastLineRay(Origin, Forward, LineRayLength, R.HandSide);
			if (bAlwaysDrawDebug) DebugDrawLineRay(Origin, Forward, LineRayLength, R.HandSide);
			break;
		}
		case EShapeType::Triangle:
		{
			const FVector Rune = Origin + Forward * TriangleSummonForwardDist;
			OnCastTriangleSummon(Rune, R.HandSide);
			if (bAlwaysDrawDebug) DebugDrawTriangleSummon(Rune, R.HandSide);
			break;
		}
		case EShapeType::VShape:
		{
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
			const FVector LeftDir  = (Forward - Right * 0.6f).GetSafeNormal();
			const FVector RightDir = (Forward + Right * 0.6f).GetSafeNormal();
			OnCastVShape(Origin, LeftDir, RightDir, R.HandSide);
			if (bAlwaysDrawDebug) DebugDrawVShape(Origin, LeftDir, RightDir, R.HandSide);
			break;
		}
		case EShapeType::Zigzag:
		{
			OnCastZigzagChain(Origin, Forward, R.HandSide);
			if (bAlwaysDrawDebug) DebugDrawZigzagChain(Origin, Forward, R.HandSide);
			break;
		}
		case EShapeType::Star:
		{
			OnCastStarProjectile(Origin, Forward, R.HandSide);
			if (bAlwaysDrawDebug) DebugDrawStarProjectile(Origin, Forward, R.HandSide);
			break;
		}
		default:
			break;
	}
}

// ============================================================
//  Debug visuals (placeholder until BP overrides with Niagara)
// ============================================================

static FColor SideColor(EHandSide S) { return S == EHandSide::Left ? FColor::Cyan : FColor::Magenta; }

void URuneSpellComponent::DebugDrawCircleShield(const FVector& Center, float Radius, float DurationSec, EHandSide Side) const
{
	UWorld* W = GetWorld();
	if (!W) return;
	DrawDebugSphere(W, Center, Radius, 24, FColor::Blue, false, DurationSec, 0, 1.5f);
	DrawDebugCircle(W, Center, Radius, 32, FColor::Cyan, false, DurationSec, 0, 1.0f, FVector(0, 1, 0), FVector(0, 0, 1), false);
}

void URuneSpellComponent::DebugDrawLineRay(const FVector& Origin, const FVector& Direction, float Length, EHandSide Side) const
{
	UWorld* W = GetWorld();
	if (!W) return;
	const FVector End = Origin + Direction * Length;
	DrawDebugLine(W, Origin, End, FColor::Yellow, false, 1.0f, 0, 4.0f);
	DrawDebugSphere(W, End, 6.0f, 12, FColor::Yellow, false, 1.0f);
}

void URuneSpellComponent::DebugDrawTriangleSummon(const FVector& RuneCenter, EHandSide Side) const
{
	UWorld* W = GetWorld();
	if (!W) return;
	DrawDebugCircle(W, RuneCenter, 30.0f, 32, FColor::Purple, false, 3.0f, 0, 2.0f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	for (int32 i = 0; i < 3; ++i)
	{
		const float A = static_cast<float>(i) * 2.0f * PI / 3.0f - PI / 2.0f;
		const FVector P = RuneCenter + FVector(FMath::Cos(A) * 30.0f, FMath::Sin(A) * 30.0f, 0.0f);
		DrawDebugSphere(W, P, 4.0f, 8, FColor::Purple, false, 3.0f);
	}
	// summoned object placeholder
	DrawDebugSphere(W, RuneCenter + FVector(0, 0, 40), 10.0f, 16, FColor::White, false, 3.0f, 0, 1.0f);
}

void URuneSpellComponent::DebugDrawVShape(const FVector& Origin, const FVector& LeftDir, const FVector& RightDir, EHandSide Side) const
{
	UWorld* W = GetWorld();
	if (!W) return;
	DrawDebugLine(W, Origin, Origin + LeftDir  * 600.0f, FColor::Orange, false, 1.0f, 0, 3.0f);
	DrawDebugLine(W, Origin, Origin + RightDir * 600.0f, FColor::Orange, false, 1.0f, 0, 3.0f);
}

void URuneSpellComponent::DebugDrawZigzagChain(const FVector& Origin, const FVector& Direction, EHandSide Side) const
{
	UWorld* W = GetWorld();
	if (!W) return;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	FVector P = Origin;
	for (int32 i = 0; i < 5; ++i)
	{
		const FVector Next = P + Direction * 80.0f + Right * ((i % 2 == 0) ? 30.0f : -30.0f);
		DrawDebugLine(W, P, Next, FColor::Cyan, false, 1.0f, 0, 3.0f);
		P = Next;
	}
}

void URuneSpellComponent::DebugDrawStarProjectile(const FVector& Origin, const FVector& Direction, EHandSide Side) const
{
	UWorld* W = GetWorld();
	if (!W) return;
	const FVector End = Origin + Direction * StarProjectileLength;
	DrawDebugLine(W, Origin, End, FColor::Red, false, 1.0f, 0, 5.0f);
	DrawDebugSphere(W, End, 25.0f, 16, FColor::Red, false, 1.5f, 0, 2.0f);
}
