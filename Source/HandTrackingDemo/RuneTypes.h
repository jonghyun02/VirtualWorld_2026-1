#pragma once

#include "CoreMinimal.h"
#include "RuneTypes.generated.h"

UENUM(BlueprintType)
enum class EShapeType : uint8
{
	None     UMETA(DisplayName = "None"),
	Circle   UMETA(DisplayName = "Circle"),
	Line     UMETA(DisplayName = "Line"),
	Triangle UMETA(DisplayName = "Triangle"),
	VShape   UMETA(DisplayName = "VShape"),
	Zigzag   UMETA(DisplayName = "Zigzag"),
	Star     UMETA(DisplayName = "Star"),
};

UENUM(BlueprintType)
enum class EHandSide : uint8
{
	Left  UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right"),
};

UENUM(BlueprintType)
enum class EDrawState : uint8
{
	Idle        UMETA(DisplayName = "Idle"),
	Drawing     UMETA(DisplayName = "Drawing"),
	Recognizing UMETA(DisplayName = "Recognizing"),
};

USTRUCT(BlueprintType)
struct FShapeRecognitionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Rune")
	EShapeType Shape = EShapeType::None;

	UPROPERTY(BlueprintReadOnly, Category = "Rune")
	float Confidence = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Rune")
	EHandSide HandSide = EHandSide::Left;

	UPROPERTY(BlueprintReadOnly, Category = "Rune")
	FVector OriginWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Rune")
	FVector ForwardWorld = FVector::ForwardVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShapeRecognized, const FShapeRecognitionResult&, Result);
