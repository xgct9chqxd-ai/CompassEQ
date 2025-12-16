#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CompassEQAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CompassEQAudioProcessorEditor (CompassEQAudioProcessor&);
    ~CompassEQAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttachment = APVTS::SliderAttachment;
    using ButtonAttachment = APVTS::ButtonAttachment;

    // ---------------- Meter Component (SAFE) ----------------
    class MeterComponent final : public juce::Component, private juce::Timer
    {
    public:
        MeterComponent (CompassEQAudioProcessor& p, bool isInputMeter)
            : proc (p), isInput (isInputMeter)
        {
            startTimerHz (30);
        }

        ~MeterComponent() override
        {
            stopTimer();
        }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();

            // background well
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRoundedRectangle (bounds, 2.0f);

            // fill
            const float v = juce::jlimit (0.0f, 1.0f, last01);
            auto fill = bounds;
            fill.removeFromTop (fill.getHeight() * (1.0f - v));

            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRoundedRectangle (fill, 2.0f);

            // border
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
        }

    private:
        void timerCallback() override
        {
            const float v = isInput ? proc.getInputMeter01() : proc.getOutputMeter01();
            last01 = juce::jlimit (0.0f, 1.0f, v);
            repaint();
        }

        CompassEQAudioProcessor& proc;
        const bool isInput = true;
        float last01 = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterComponent)
    };

    // ---------------- Helpers ----------------
    void configureKnob (juce::Slider&);

    // ---------------- References ----------------
    CompassEQAudioProcessor& proc;
    APVTS& apvts;

    // ---------------- Controls ----------------
    juce::Slider lfFreq, lfGain;
    juce::Slider lmfFreq, lmfGain, lmfQ;
    juce::Slider hmfFreq, hmfGain, hmfQ;
    juce::Slider hfFreq, hfGain;

    juce::Slider hpfFreq, lpfFreq;

    juce::Slider inTrim, outTrim;

    juce::ToggleButton globalBypass;

    // ---------------- Attachments ----------------
    std::unique_ptr<SliderAttachment> attLfFreq, attLfGain;
    std::unique_ptr<SliderAttachment> attLmfFreq, attLmfGain, attLmfQ;
    std::unique_ptr<SliderAttachment> attHmfFreq, attHmfGain, attHmfQ;
    std::unique_ptr<SliderAttachment> attHfFreq, attHfGain;

    std::unique_ptr<SliderAttachment> attHpfFreq, attLpfFreq;

    std::unique_ptr<SliderAttachment> attInTrim, attOutTrim;

    std::unique_ptr<ButtonAttachment> attBypass;

    // ---------------- Meters ----------------
    MeterComponent inputMeter;
    MeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassEQAudioProcessorEditor)
};
