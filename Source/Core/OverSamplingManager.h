#pragma once
#include <JuceHeader.h>

namespace compass
{
class OversamplingManager
{
public:
    OversamplingManager() = default;
    ~OversamplingManager() = default;

    void prepare (const juce::dsp::ProcessSpec&) {}
    void reset() {}
};
} // namespace compass
