#pragma once

#include <JuceHeader.h>
#include <functional>
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
            const auto b = getLocalBounds();

            // Hard occlude anything behind the meter lane
            g.setColour (juce::Colours::black);
            g.fillRect (b);

            const auto bounds = b.toFloat().reduced (1.0f);

            // LED ladder config (SSL-ish)
            constexpr int kDots   = 23;
            constexpr int kGreen  = 16;
            constexpr int kYellow = 5;
            constexpr int kRed    = 2;

            const float v01 = juce::jlimit (0.0f, 1.0f, last01);
            const int litDots = juce::jlimit (0, kDots, (int) std::lround (v01 * (float) kDots));

            const float w = bounds.getWidth();
            const float h = bounds.getHeight();

            // We FORCE full-height coverage.
            constexpr float minGap = 1.0f;

            // Start with a diameter that fits width, then clamp by height constraint.
            float dotD = juce::jlimit (2.5f, 7.0f, w - 4.0f);

            // If height can't accommodate with minGap, shrink dotD until it can.
            const float maxDotDByHeight = (h - minGap * (float) (kDots - 1)) / (float) kDots;
            dotD = juce::jmin (dotD, maxDotDByHeight);

            // Now compute the exact gap that makes the ladder span the full height.
            float gap = (h - dotD * (float) kDots) / (float) (kDots - 1);
            gap = juce::jmax (minGap, gap);

            // Recompute dotD once more in case we clamped gap up.
            dotD = (h - gap * (float) (kDots - 1)) / (float) kDots;

            const float x = bounds.getX() + (w - dotD) * 0.5f;

            // Anchor bottom dot to bottom edge
            const float yBottom = bounds.getBottom() - dotD;

            // “On” colors (lit)
            const auto greenOn  = juce::Colour::fromRGB (60, 200, 110).withAlpha (0.90f);
            const auto yellowOn = juce::Colour::fromRGB (230, 200, 70).withAlpha (0.90f);
            const auto redOn    = juce::Colour::fromRGB (230, 70, 70).withAlpha (0.95f);

            // “Off” colors (dim but still color-coded)
            const auto greenOff  = juce::Colour::fromRGB (60, 200, 110).withAlpha (0.14f);
            const auto yellowOff = juce::Colour::fromRGB (230, 200, 70).withAlpha (0.14f);
            const auto redOff    = juce::Colour::fromRGB (230, 70, 70).withAlpha (0.16f);

            for (int i = 0; i < kDots; ++i)
            {
                const bool on = (i < litDots);

                juce::Colour c;
                if (i < kGreen)              c = on ? greenOn  : greenOff;
                else if (i < kGreen+kYellow) c = on ? yellowOn : yellowOff;
                else                         c = on ? redOn    : redOff;

                const float y = yBottom - (float) i * (dotD + gap);

                g.setColour (c);
                g.fillRoundedRectangle (juce::Rectangle<float> (x, y, dotD, dotD), dotD * 0.30f);
            }

            // Border decision: OFF for now (dots only)
            // If we want it later, we'll add it back after we judge the look.
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

    // ---------------- Floating Value Popup (Phase X) ----------------
    juce::Label valuePopup;
    juce::Slider* activeSlider = nullptr;

    class AltClickToggle final : public juce::ToggleButton
    {
    public:
        std::function<void()> onAltClick;

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isAltDown())
            {
                if (onAltClick) onAltClick();
                return; // do NOT call base => bypass must NOT toggle
            }

            juce::ToggleButton::mouseUp (e);
        }
    };

    AltClickToggle globalBypass;

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
