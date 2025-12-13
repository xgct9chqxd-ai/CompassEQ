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

        void prepare(const juce::dsp::ProcessSpec &spec) { dsp.prepare(spec); }
        void process(juce::AudioBuffer<float> &buffer) { dsp.process(buffer); }

        void setParameterPointers(std::atomic<float> *inTrim,
                                  std::atomic<float> *outTrim,
                                  std::atomic<float> *hpfFreq,
                                  std::atomic<float> *lpfFreq) noexcept
        {
            dsp.setParameterPointers(inTrim, outTrim, hpfFreq, lpfFreq);
        }

    private:
        DSPCore dsp;
    };
} // namespace compass