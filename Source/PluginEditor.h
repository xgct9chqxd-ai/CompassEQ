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

    // ---------------- Asset Slots (Phase 4.0) ----------------
    struct AssetSlots
    {
        juce::Rectangle<int> editor;

        // Major regions (derived from existing component bounds)
        juce::Rectangle<int> headerZone;
        juce::Rectangle<int> filtersZone;
        juce::Rectangle<int> bandsZone;
        juce::Rectangle<int> trimZone;

        // Components (exact bounds)
        juce::Rectangle<int> inputMeter;
        juce::Rectangle<int> outputMeter;

        juce::Rectangle<int> hpfKnob;
        juce::Rectangle<int> lpfKnob;

        juce::Rectangle<int> lfFreq, lfGain;
        juce::Rectangle<int> lmfFreq, lmfGain, lmfQ;
        juce::Rectangle<int> hmfFreq, hmfGain, hmfQ;
        juce::Rectangle<int> hfFreq, hfGain;

        juce::Rectangle<int> inTrim;
        juce::Rectangle<int> outTrim;
        juce::Rectangle<int> bypass;

        // Column unions (useful for later “panel” assets)
        juce::Rectangle<int> colLF;
        juce::Rectangle<int> colLMF;
        juce::Rectangle<int> colHMF;
        juce::Rectangle<int> colHF;

        // Whole control unions
        juce::Rectangle<int> filtersUnion;
        juce::Rectangle<int> bandsUnion;
        juce::Rectangle<int> trimsUnion;
    };

    AssetSlots assetSlots;

    // Debug overlay (OFF by default)
    // Set to 1 manually if you want temporary outlines during asset integration.
    static constexpr int kAssetSlotDebug = 0;

    // ---------------- Meters ----------------
    MeterComponent inputMeter;
    MeterComponent outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassEQAudioProcessorEditor)
};
