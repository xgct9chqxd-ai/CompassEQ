#pragma once
#include <JuceHeader.h>

namespace compass
{
class MeterBus
{
public:
    MeterBus() = default;
    ~MeterBus() = default;

    void pushBlock (const juce::AudioBuffer<float>&) {}
};
} // namespace compass
