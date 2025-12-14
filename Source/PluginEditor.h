#pragma once
#include <JuceHeader.h>

// Best-practice: forward declare to avoid header include cycles.
class CompassEQAudioProcessor;

class LevelMeter final : public juce::Component
{
public:
    void setLevel01(float v) noexcept
    {
        level01.store(v, std::memory_order_relaxed);
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);

        auto b = getLocalBounds().reduced(2);
        g.setColour(juce::Colours::darkgrey);
        g.drawRect(b, 1);

        const float v = juce::jlimit(0.0f, 1.0f, level01.load(std::memory_order_relaxed));
        const int fillH = juce::roundToInt(v * (float)b.getHeight());

        auto fill = b.removeFromBottom(fillH);
        g.setColour(juce::Colours::lightgrey); // Phase 1 UI Addendum allows neutral colors
        g.fillRect(fill);
    }

private:
    std::atomic<float> level01{0.0f};
};

class CompassEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit CompassEQAudioProcessorEditor(CompassEQAudioProcessor &);
    ~CompassEQAudioProcessorEditor() override = default;

    void paint(juce::Graphics &) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;

    // NOTE: renamed from "processor" to avoid shadowing AudioProcessorEditor::processor
    CompassEQAudioProcessor &proc;
    juce::AudioProcessorValueTreeState &apvts;

    // Controls (required)
    juce::Slider lfFreq, lfGain;
    juce::Slider lmfFreq, lmfGain, lmfQ;
    juce::Slider hmfFreq, hmfGain, hmfQ;
    juce::Slider hfFreq, hfGain;

    juce::Slider hpfFreq, lpfFreq;
    juce::Slider inTrim, outTrim;

    juce::ToggleButton bypass;

    // Meters (exactly 2)
    LevelMeter inMeter, outMeter;

    // Attachments
    std::unique_ptr<SliderAttachment> aLfFreq, aLfGain;
    std::unique_ptr<SliderAttachment> aLmfFreq, aLmfGain, aLmfQ;
    std::unique_ptr<SliderAttachment> aHmfFreq, aHmfGain, aHmfQ;
    std::unique_ptr<SliderAttachment> aHfFreq, aHfGain;

    std::unique_ptr<SliderAttachment> aHpfFreq, aLpfFreq;
    std::unique_ptr<SliderAttachment> aInTrim, aOutTrim;

    std::unique_ptr<ButtonAttachment> aBypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompassEQAudioProcessorEditor)
};
