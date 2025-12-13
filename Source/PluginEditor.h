#pragma once

#include <JuceHeader.h>

namespace compass
{
class CompassEQAudioProcessor;

class CompassEQAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CompassEQAudioProcessorEditor (CompassEQAudioProcessor&);
    ~CompassEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CompassEQAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassEQAudioProcessorEditor)
};
} // namespace compass
