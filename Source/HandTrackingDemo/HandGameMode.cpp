#include "HandGameMode.h"
#include "HandPawn.h"

AHandGameMode::AHandGameMode()
{
	DefaultPawnClass = AHandPawn::StaticClass();
}
