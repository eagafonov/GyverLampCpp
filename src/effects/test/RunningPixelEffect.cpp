#include "RunningPixelEffect.h"

RunningPixelEffect::RunningPixelEffect(const String &id)
    : Effect(id), pixelIndex(0)
{

}

void RunningPixelEffect::tick()
{
    myMatrix->clear();

    const auto stepsCount = min(mySettings->matrixSettings.width, mySettings->matrixSettings.height);

    myMatrix->drawPixelXY(pixelIndex % stepsCount,
                          pixelIndex % stepsCount,
                          pixelIndex == 0 ? CRGB::Red : CRGB::White);

    pixelIndex = (pixelIndex + 1) % stepsCount;
}

