#pragma once
#include <JuceHeader.h>

namespace compass
{
class ParameterState
{
public:
    explicit ParameterState (juce::AudioProcessor& processor);
    ~ParameterState() = default;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts; }

    // Parameter IDs (LOCKED for Phase 2A)
    static constexpr const char* kInputTrimId  = "inputTrim";
    static constexpr const char* kOutputTrimId = "outputTrim";
    static constexpr const char* kHpfFreqId    = "hpfFreq";
    static constexpr const char* kLpfFreqId    = "lpfFreq";

private:
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterState)
};
} // namespace compass
