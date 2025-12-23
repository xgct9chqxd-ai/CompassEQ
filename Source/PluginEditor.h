#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <functional>
#include "PluginProcessor.h"
#include "UIStyle.h"

class CompassEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::AsyncUpdater
{
public:
    explicit CompassEQAudioProcessorEditor (CompassEQAudioProcessor&);
    ~CompassEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void handleAsyncUpdate() override;
    void renderStaticLayer (juce::Graphics& g, float scaleKey, float physicalScale);
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttachment = APVTS::SliderAttachment;
    using ButtonAttachment = APVTS::ButtonAttachment;

    // ---------------- Meter Component (SAFE) ----------------
    class MeterComponent final : public juce::Component, private juce::Timer
    {
    public:
        MeterComponent (CompassEQAudioProcessor& p, bool isInputMeter, CompassEQAudioProcessorEditor& e)
            : proc (p), isInput (isInputMeter), editor (e)
        {
            startTimerHz (30);
        }

        ~MeterComponent() override
        {
            stopTimer();
        }

        // Phase 4: Stop timer when component becomes invisible
        void visibilityChanged() override
        {
            if (! isVisible())
                stopTimer();
            else if (isTimerRunning() == false)
                startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            // Phase 3C: PERFORMANCE BUDGET VERIFICATION CHECKLIST
            // ✓ No std::vector growth, no std::string concat, no Path churn that allocates
            // ✓ No Image reallocation in paint, no Gradient objects created per-frame without reuse/caching
            // ✓ Fonts from ladders/tokens (not applicable, meter uses no text)
            // ✓ All coordinates are stack-allocated floats/ints
            // ✓ fillRoundedRectangle uses stack-allocated parameters only
            // ✓ Meter repaint is isolated (Timer-based, repaint() only affects this component, never triggers editor-wide repaint)
            
            // Phase 2: Get scaleKey and physical scale for snapping
            const float scaleKey = editor.getScaleKeyActive();
            const float physicalScale = juce::jmax (1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());

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

            // Phase 2: Use discrete ladder for min gap
            const float minGap = UIStyle::MeterLadder::dotGapMin (scaleKey);

            // Phase 2: Use discrete ladder for dot size range
            const float dotSizeMin = UIStyle::MeterLadder::dotSizeMin (scaleKey);
            const float dotSizeMax = UIStyle::MeterLadder::dotSizeMax (scaleKey);

            // Start with a diameter that fits width, then clamp by height constraint.
            float dotD = juce::jlimit (dotSizeMin, dotSizeMax, w - 4.0f);

            // If height can't accommodate with minGap, shrink dotD until it can.
            const float maxDotDByHeight = (h - minGap * (float) (kDots - 1)) / (float) kDots;
            dotD = juce::jmin (dotD, maxDotDByHeight);

            // Now compute the exact gap that makes the ladder span the full height.
            float gap = (h - dotD * (float) kDots) / (float) (kDots - 1);
            gap = juce::jmax (minGap, gap);

            // Recompute dotD once more in case we clamped gap up.
            dotD = (h - gap * (float) (kDots - 1)) / (float) kDots;

            // Phase 2: Snap dot center X and spacing
            const float x = UIStyle::Snap::snapPx (bounds.getX() + (w - dotD) * 0.5f, physicalScale);

            // Anchor bottom dot to bottom edge - Phase 2: Snap Y positions
            const float yBottom = UIStyle::Snap::snapPx (bounds.getBottom() - dotD, physicalScale);

            // "On" colors (lit)
            const auto greenOn  = juce::Colour::fromRGB (60, 200, 110).withAlpha (0.90f);
            const auto yellowOn = juce::Colour::fromRGB (230, 200, 70).withAlpha (0.90f);
            const auto redOn    = juce::Colour::fromRGB (230, 70, 70).withAlpha (0.95f);

            // "Off" colors (dim but still color-coded)
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

                // Phase 2: Snap dot Y position
                const float y = UIStyle::Snap::snapPx (yBottom - (float) i * (dotD + gap), physicalScale);

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
        CompassEQAudioProcessorEditor& editor;
        const bool isInput;
        float last01 = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterComponent)
    };

    // ---------------- Phase 6: CompassSlider (Shift fine adjust + double-click reset) ----------------
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

            // Shift path only: compressed velocity for fine control (no change to non-Shift behavior)
            if (e.mods.isShiftDown())
            {
                // Anti-skip limiter: clamp per-event delta in Shift mode so bursts can't jump detents.
                const float maxDeltaPxPerEvent = 6.0f; // Shift mode cap (single-scale)
                deltaPixels = (int) juce::jlimit (-maxDeltaPxPerEvent, maxDeltaPxPerEvent, (float) deltaPixels);

                // vCap is constant because UI is single-scale.
                const float vCap = 30.0f;
                const float v = (float) std::abs (deltaPixels);
                const float t = juce::jlimit (0.0f, 1.0f, v / vCap);
                const float compressed = std::sqrt (t);

                const float shiftMin = 0.28f;
                const float shiftMax = 0.62f;
                const float shiftSensitivity = juce::jmap (compressed, shiftMin, shiftMax);

                // Update only the Shift velocity-mode sensitivity. (Normal drag path is unchanged.)
                setVelocityModeParameters (shiftSensitivity, 0, 0.0, true, juce::ModifierKeys::shiftModifier);
            }

            juce::Slider::mouseDrag (e);
        }

    private:
        int lastDragY = 0;
    };

    // ---------------- Phase 6: Fixed Value Readout (allocation-safe, fixed bounds) ----------------
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
            // Phase 6: Store in char buffer to avoid allocations
            text.copyToUTF8 (textBuffer, sizeof (textBuffer));
            textBuffer[sizeof (textBuffer) - 1] = '\0';
            repaint();
        }
        
        void show()
        {
            if (! isVisible())
            {
                setVisible (true);
                repaint();
            }
        }
        
        void hide()
        {
            if (isVisible())
            {
                setVisible (false);
                textBuffer[0] = '\0';
                repaint();
            }
        }
        
        void paint (juce::Graphics& g) override
        {
            if (textBuffer[0] == '\0')
                return;
            
            // Phase 6: Get scaleKey and physicalScale for snapping
            const float scaleKey = editor.getScaleKeyActive();
            const float physicalScale = juce::jmax (1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());
            
            // Phase 6: Use StringRef to avoid allocations, snap baseline Y
            g.setColour (UIStyle::Colors::foreground.withAlpha (UIStyle::TextAlpha::header));
            const auto& font = UIStyle::FontLadder::headerFont (scaleKey);
            g.setFont (font);
            
            auto bounds = getLocalBounds();
            const float snappedY = UIStyle::Snap::snapPx ((float) bounds.getY(), physicalScale);
            bounds.setY ((int) snappedY);
            g.drawText (juce::StringRef (textBuffer), bounds, juce::Justification::centred, false);
        }
        
    private:
        CompassEQAudioProcessorEditor& editor;
        char textBuffer[64] = {0};
    };

    // ---------------- Helpers ----------------
    void configureKnob (CompassSlider&, const char* paramId, float defaultValue);

    // ---------------- References ----------------
    CompassEQAudioProcessor& proc;
    APVTS& apvts;

    // ---------------- Controls ----------------
    CompassSlider lfFreq, lfGain;
    CompassSlider lmfFreq, lmfGain, lmfQ;
    CompassSlider hmfFreq, hmfGain, hmfQ;
    CompassSlider hfFreq, hfGain;

    CompassSlider hpfFreq, lpfFreq;

    CompassSlider inTrim, outTrim;

    // ---------------- Phase 6: Fixed Value Readout (replaces moving popup) ----------------
    ValueReadout valueReadout;
    CompassSlider* activeSlider = nullptr;
    
    // Phase 6: Fixed readout bounds (set in resized(), never changes)
    static constexpr int kReadoutX = 300;
    static constexpr int kReadoutY = 30;
    static constexpr int kReadoutW = 160;
    static constexpr int kReadoutH = 18;

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

        void paintButton (juce::Graphics& g, bool /*shouldDrawButtonAsHighlighted*/, bool shouldDrawButtonAsDown) override
        {
            const auto b = getLocalBounds();
            if (b.isEmpty())
                return;

            const bool isOn = getToggleState();
            (void) shouldDrawButtonAsDown; // no hover/pressed styling (LOCKED)

            // SSL BYPASS button — drop-in spec (LOCKED)
            //
            // Geometry:
            //   r = getLocalBounds().toFloat().reduced(0.5f);
            //   Outer border stroke: 1.0f
            //   Inner edge stroke: 1.0f on r.reduced(1.0f)
            const auto r = b.toFloat().reduced (0.5f);

            // Colors (LOCKED CONSTANTS)
            const auto border = juce::Colour::fromRGB (120, 120, 120);
            const auto innerEdge = juce::Colours::black.withAlpha (0.18f);

            const auto offFill = juce::Colour::fromRGB (210, 210, 210);
            const auto offText = juce::Colour::fromRGB (12, 12, 12);

            const auto onFill = juce::Colour::fromRGB (210, 210, 210);
            const auto glowC = juce::Colour::fromRGB (160, 235, 195);
            const auto onText = juce::Colour::fromRGB (12, 12, 12);

            // 1) Fill base (OFF/ON both use off-white base)
            g.setColour (isOn ? onFill : offFill);
            g.fillRect (r);

            // 2) If ON: draw glow1, then glow2 (inset overlays; flat rectangles; no gradients)
            if (isOn)
            {
                g.setColour (glowC.withAlpha (0.22f));
                g.fillRect (r.reduced (2.0f));

                g.setColour (glowC.withAlpha (0.14f));
                g.fillRect (r.reduced (5.0f));
            }

            // 3) Draw 1px outer border
            g.setColour (border);
            g.drawRect (r, 1.0f);

            // 4) Draw 1px inner darkening stroke
            g.setColour (innerEdge);
            g.drawRect (r.reduced (1.0f), 1.0f);

            // 5) Draw centered text (same sizing as current; do not enlarge)
            g.setColour (isOn ? onText : offText);
            g.setFont (12.0f);
            g.drawFittedText (getButtonText(), b.reduced (6, 0), juce::Justification::centred, 1);
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

    // ---------------- Custom LookAndFeel ----------------
    class CompassLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit CompassLookAndFeel (CompassEQAudioProcessorEditor& e) : editor (e) {}
        
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                               juce::Slider&) override;
    private:
        CompassEQAudioProcessorEditor& editor;
    };

    std::unique_ptr<CompassLookAndFeel> lookAndFeel;

    // ---------------- Scale Management (Phase 1) ----------------
    float getPhysicalScaleLastPaint() const { return physicalScaleLastPaint; }
    // UI is single-scale (1.0f). Do not add DPI scaling without reopening UI/knob gates.
    float getScaleKeyActive() const { return 1.0f; }

    // Scale state machine (stability + rate limiting)
    float physicalScaleLastPaint = 1.0f;
    float scaleKeyActive = 1.0f;
    
    // Stability window: track last N scaleKey values
    static constexpr int stabilityWindowSize = 3;
    float scaleKeyHistory[stabilityWindowSize] = { 1.0f, 1.0f, 1.0f };
    int scaleKeyHistoryIndex = 0;
    int scaleKeyHistoryCount = 0;  // How many valid entries we have
    
    // Rate limiting
    juce::int64 lastScaleKeyChangeTime = 0;
    static constexpr juce::int64 rateLimitMs = 250;

    // ---------------- Static Layer Cache (Phase 3A) ----------------
    // Cache key = (scaleKeyActive, physical pixel size w*h)
    // Cache rebuilds ONLY when: scaleKeyActive changes, resized() changes bounds, or explicit invalidation
    // Cache rebuild happens via AsyncUpdater on UI thread, NEVER in paint()
    struct StaticLayerCache
    {
        float scaleKey = 0.0f;
        int pixelW = 0;
        int pixelH = 0;
        juce::Image image; // ARGB
        bool valid() const { return image.isValid() && pixelW > 0 && pixelH > 0; }
        void clear() { image = {}; scaleKey = 0.0f; pixelW = 0; pixelH = 0; }
    };

    StaticLayerCache staticCache;
    std::atomic<bool> staticCacheDirty { true };
    std::atomic<bool> staticCacheRebuildPending { false };

    // Phase 4: Teardown safety guard (UI thread only, no need for atomic)
    bool isTearingDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassEQAudioProcessorEditor)
};
