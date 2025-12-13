#pragma once
#include <JuceHeader.h>
#include "DSPCore.h"

namespace compass
{
class Router
{
public:
    Router() = default;
    ~Router() = default;

    void prepare (const juce::dsp::ProcessSpec& spec) { dsp.prepare (spec); }
    void process (juce::AudioBuffer<float>& buffer)   { dsp.process (buffer); }

private:
    DSPCore dsp;
};
} // namespace compass
