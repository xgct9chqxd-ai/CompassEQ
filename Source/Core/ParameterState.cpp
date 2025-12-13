#include "ParameterState.h"

namespace compass
{
namespace
{
    inline juce::NormalisableRange<float> makeFreqRange()
    {
        // Keep simple + safe for now. We can align exact constitution ranges later,
        // but these are sane defaults for Phase 2A bring-up.
        return { 20.0f, 20000.0f, 0.0f, 0.5f };
    }
}

ParameterState::ParameterState(juce::AudioProcessor &processor)
    : apvts(processor, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout ParameterState::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Trims (dB)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        kInputTrimId,
        "Input Trim",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f },
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        kOutputTrimId,
        "Output Trim",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.01f },
        0.0f));

    // Filters (Hz)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        kHpfFreqId,
        "HPF Frequency",
        makeFreqRange(),
        20.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        kLpfFreqId,
        "LPF Frequency",
        makeFreqRange(),
        20000.0f));

    return layout;
}
} // namespace compass
