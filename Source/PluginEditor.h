#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <functional>
#include "PluginProcessor.h"
#include "CompassLookAndFeel.h"
#include "UIStyle.h"

class CompassEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::AsyncUpdater
{
public:
    explicit CompassEQAudioProcessorEditor (CompassEQAudioProcessor&);
    ~CompassEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics& g) override;

    void resized() override;

private:
    // --- Internal Logic ---
    void handleAsyncUpdate() override;
    void renderStaticLayer (juce::Graphics& g, float scaleKey, float physicalScale);
    void ensureBackgroundNoiseTile();
    void ensureCosmicHaze();

    // --- Assets ---
    juce::Image backgroundGrainTexture;
    juce::Image cosmicHazeTexture;

    // --- Aliases ---
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttachment = APVTS::SliderAttachment;
    using ButtonAttachment = APVTS::ButtonAttachment;

    // ==============================================================================
    // NESTED COMPONENT: METER (Stereo, Industrial Style)
    // ==============================================================================
    class MeterComponent final : public juce::Component, private juce::Timer
    {
    public:
        MeterComponent (CompassEQAudioProcessor& p, bool isInputMeter, CompassEQAudioProcessorEditor&)
            : proc (p), isInput (isInputMeter)
        {
            startTimerHz (30);
        }

        ~MeterComponent() override { stopTimer(); }

        void visibilityChanged() override
        {
            if (! isVisible()) stopTimer();
            else if (! isTimerRunning()) startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            
            // Stereo Setup: Split width for L/R channels
            const float gap = 2.0f;
            const float barW = (bounds.getWidth() - gap) * 0.5f;
            
            auto leftRect = bounds.removeFromLeft(barW);
            auto rightRect = bounds.removeFromRight(barW);
            
            drawChannel(g, leftRect, currentValL);
            drawChannel(g, rightRect, currentValR);
        }

    private:
        void drawChannel(juce::Graphics& g, juce::Rectangle<float> r, float value01)
        {
            constexpr int kSegN = 44;
            constexpr float kSegGap = 1.0f;
            constexpr float kMinSegH = 1.0f;

            // Limiter Palette
            const juce::Colour cGrey  = juce::Colour::fromFloatRGBA(0.62f, 0.62f, 0.62f, 1.0f);
            const juce::Colour cGreen = juce::Colour::fromFloatRGBA(0.30f, 0.68f, 0.46f, 1.0f);
            const juce::Colour cYell  = juce::Colour::fromFloatRGBA(0.95f, 0.86f, 0.40f, 1.0f);
            const juce::Colour cAmber = juce::Colour::fromFloatRGBA(0.78f, 0.44f, 0.18f, 1.0f);
            const juce::Colour cRed   = juce::Colour::fromFloatRGBA(0.90f, 0.22f, 0.12f, 1.0f);

            constexpr float kDbFloor = -60.0f;
            constexpr float kDbCeil = 6.0f;
            constexpr float kDbSpan = kDbCeil - kDbFloor;
            constexpr float kGreenTopDb = -6.0f;
            constexpr float kYellowTopDb = 0.0f;

            const float totalGapH = kSegGap * (float)(kSegN - 1);
            const float segH = juce::jmax(kMinSegH, (r.getHeight() - totalGapH) / (float)kSegN);
            
            float db = (value01 > 0.00001f) ? juce::Decibels::gainToDecibels(value01) : kDbFloor;
            const float vNorm = juce::jlimit(0.0f, 1.0f, (db - kDbFloor) / kDbSpan);
            const int litN = (int)(vNorm * kSegN);

            for (int i = 0; i < kSegN; ++i)
            {
                const int idxFromBottom = i;
                const float y = r.getBottom() - (float)(idxFromBottom + 1) * segH - (float)idxFromBottom * kSegGap;
                juce::Rectangle<float> seg(r.getX(), y, r.getWidth(), segH);
                
                const bool isActive = (idxFromBottom < litN);
                
                const float segDb = kDbFloor + ((float)(i + 1) / kSegN) * kDbSpan;
                
                juce::Colour base;
                if (segDb <= kGreenTopDb) base = cGreen;
                else if (segDb <= kYellowTopDb) base = cGreen.interpolatedWith(cYell, juce::jlimit(0.0f, 1.0f, (segDb - kGreenTopDb) / (kYellowTopDb - kGreenTopDb)));
                else base = cAmber.interpolatedWith(cRed, juce::jlimit(0.0f, 1.0f, (segDb - kYellowTopDb) / (kDbCeil - kYellowTopDb)));

                if (isActive) g.setColour(base.interpolatedWith(cGrey, 0.55f).withAlpha(0.70f)); // Desaturated & dimmer
                else g.setColour(cGrey.withAlpha(0.12f));
                
                g.fillRoundedRectangle(seg, 1.0f);
            }
        }

        void timerCallback() override
        {
            // Simulate stereo values until DSP provides separate channels
            const float vRaw = isInput ? proc.getInputMeter01() : proc.getOutputMeter01();
            
            // Map mono DSP value to stereo visuals for now
            float target = (vRaw <= 0.0f) ? 0.0f : juce::jlimit(0.0f, 1.0f, std::sqrt(vRaw));
            
            currentValL = target;
            currentValR = target; // In future, hook this to proc.getRightMeter()
            repaint();
        }

        CompassEQAudioProcessor& proc;
        const bool isInput;
        float currentValL = 0.0f;
        float currentValR = 0.0f;
    };

    // ==============================================================================
    // NESTED COMPONENT: CUSTOM SLIDER (Shift-Drag)
    // ==============================================================================
    class CompassSlider final : public juce::Slider
    {
    public:
        CompassSlider() = default;

        void mouseDown (const juce::MouseEvent& e) override
        {
            lastDragY = e.getPosition().y;
            juce::Slider::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const int y = e.getPosition().y;
            int deltaPixels = y - lastDragY;
            lastDragY = y;

            if (e.mods.isShiftDown())
            {
                const float maxDeltaPxPerEvent = 6.0f;
                deltaPixels = (int) juce::jlimit (-maxDeltaPxPerEvent, maxDeltaPxPerEvent, (float) deltaPixels);
                const float vCap = 30.0f;
                const float v = (float) std::abs (deltaPixels);
                const float t = juce::jlimit (0.0f, 1.0f, v / vCap);
                const float compressed = std::sqrt (t);
                const float shiftMin = 0.28f;
                const float shiftMax = 0.62f;
                const float shiftSensitivity = juce::jmap (compressed, shiftMin, shiftMax);
                setVelocityModeParameters (shiftSensitivity, 0, 0.0, true, juce::ModifierKeys::shiftModifier);
            }
            juce::Slider::mouseDrag (e);
        }

    private:
        int lastDragY = 0;
    };

    // ==============================================================================
    // NESTED COMPONENT: VALUE POPUP
    // ==============================================================================
    class ValueReadout final : public juce::Component
    {
    public:
        ValueReadout (CompassEQAudioProcessorEditor& e) : editor (e)
        {
            setInterceptsMouseClicks (false, false);
            setVisible (false);
        }
        
        void setValueText (const juce::String& text)
        {
            text.copyToUTF8 (textBuffer, sizeof (textBuffer));
            textBuffer[sizeof (textBuffer) - 1] = '\0';
            repaint();
        }
        
        void show() { if (! isVisible()) { setVisible (true); repaint(); } }
        void hide() { if (isVisible()) { setVisible (false); textBuffer[0] = '\0'; repaint(); } }
        
        void paint (juce::Graphics& g) override
        {
            if (textBuffer[0] == '\0') return;
            
            const float scaleKey = editor.getScaleKeyActive();
            const float physicalScale = juce::jmax (1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());
            
            auto bounds = getLocalBounds().toFloat();
            const float px = 1.0f / physicalScale;
            const auto& font = UIStyle::FontLadder::headerFont (scaleKey);
            g.setFont (font.withHeight (font.getHeight() * 1.3f).withExtraKerningFactor (-0.04f));

            const float snappedY = UIStyle::Snap::snapPx (bounds.getY(), physicalScale);
            bounds.setY (snappedY + (2.0f * px));

            g.setColour (juce::Colours::black.withAlpha (0.80f));
            g.drawText (juce::StringRef (textBuffer), bounds.translated (1.2f * px, 1.2f * px), juce::Justification::centred, false);
            g.setColour (juce::Colour (0xFFE8E8E8));
            g.drawText (juce::StringRef (textBuffer), bounds, juce::Justification::centred, false);
        }
        
    private:
        CompassEQAudioProcessorEditor& editor;
        char textBuffer[64] = {0};
    };

    // ---------------- Alt Click Toggle ----------------
    class AltClickToggle final : public juce::ToggleButton
    {
    public:
        std::function<void()> onAltClick;
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isAltDown()) { if (onAltClick) onAltClick(); return; }
            juce::ToggleButton::mouseUp (e);
        }
        void paintButton (juce::Graphics&, bool, bool) override {}
    };

    // ---------------- References & Members ----------------
    CompassEQAudioProcessor& proc;
    APVTS& apvts;

    // Controls
    CompassSlider lfFreq, lfGain;
    CompassSlider lmfFreq, lmfGain, lmfQ;
    CompassSlider hmfFreq, hmfGain, hmfQ;
    CompassSlider hfFreq, hfGain;
    CompassSlider hpfFreq, lpfFreq;
    CompassSlider inTrim, outTrim;

    ValueReadout valueReadout;
    CompassSlider* activeSlider = nullptr;
    
    AltClickToggle globalBypass;

    std::unique_ptr<SliderAttachment> attLfFreq, attLfGain;
    std::unique_ptr<SliderAttachment> attLmfFreq, attLmfGain, attLmfQ;
    std::unique_ptr<SliderAttachment> attHmfFreq, attHmfGain, attHmfQ;
    std::unique_ptr<SliderAttachment> attHfFreq, attHfGain;
    std::unique_ptr<SliderAttachment> attHpfFreq, attLpfFreq;
    std::unique_ptr<SliderAttachment> attInTrim, attOutTrim;
    std::unique_ptr<ButtonAttachment> attBypass;

    // Layout
    struct AssetSlots
    {
        juce::Rectangle<int> editor, headerZone, filtersZone, bandsZone, trimZone;
        juce::Rectangle<int> inputMeter, outputMeter;
        juce::Rectangle<int> hpfKnob, lpfKnob;
        juce::Rectangle<int> lfFreq, lfGain;
        juce::Rectangle<int> lmfFreq, lmfGain, lmfQ;
        juce::Rectangle<int> hmfFreq, hmfGain, hmfQ;
        juce::Rectangle<int> hfFreq, hfGain;
        juce::Rectangle<int> inTrim, outTrim, bypass;
        juce::Rectangle<int> colLF, colLMF, colHMF, colHF;
        juce::Rectangle<int> filtersUnion, bandsUnion, trimsUnion;
    };
    AssetSlots assetSlots;

    MeterComponent inputMeter;
    MeterComponent outputMeter;

    std::unique_ptr<CompassLookAndFeel> lookAndFeel;

    // Scale & Caching Logic
    float getPhysicalScaleLastPaint() const { return physicalScaleLastPaint; }
    float getScaleKeyActive() const { return 1.0f; }

    float physicalScaleLastPaint = 1.0f;
    float scaleKeyActive = 1.0f;
    
    static constexpr int stabilityWindowSize = 3;
    float scaleKeyHistory[stabilityWindowSize] = { 1.0f, 1.0f, 1.0f };
    int scaleKeyHistoryIndex = 0;
    int scaleKeyHistoryCount = 0;
    
    juce::int64 lastScaleKeyChangeTime = 0;
    
    // --- Helpers Declaration ---
    void configureKnob(CompassSlider& s, const char* paramId, float defaultValue);

    struct StaticLayerCache
    {
        float scaleKey = 0.0f;
        int pixelW = 0;
        int pixelH = 0;
        juce::Image image;
        bool valid() const { return image.isValid() && pixelW > 0 && pixelH > 0; }
        void clear() { image = {}; scaleKey = 0.0f; pixelW = 0; pixelH = 0; }
    };

    StaticLayerCache staticCache;
    std::atomic<bool> staticCacheDirty { true };
    std::atomic<bool> staticCacheRebuildPending { false };

    bool isTearingDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassEQAudioProcessorEditor)
};
