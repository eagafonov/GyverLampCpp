#pragma once
#include "effects/Effect.h"

class RunningPixelEffect : public Effect
{
public:
    explicit RunningPixelEffect(const String &id);
    void tick() override;

private:
    int pixelIndex;
};

