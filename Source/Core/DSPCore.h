#pragma once
#include <JuceHeader.h>

namespace compass
{
class DSPCore
{
public:
    DSPCore() = default;
    ~DSPCore() = default;

    void prepare (const juce::dsp::ProcessSpec&) {}
    void process (juce::AudioBuffer<float>&) {}
};
} // namespace compass
