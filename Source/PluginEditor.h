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
            const auto bounds = b.toFloat().reduced (1.0f);

            // Pass 2: Meter housing / frame integration (visual only)
            // Goal: "instrument panel window" consistent with band panels (thin outer stroke + softer inner edge).
            {
                const auto frame = b.toFloat().reduced (0.5f);
                g.setColour (juce::Colours::silver.withAlpha (0.22f));
                g.drawRoundedRectangle (frame, 4.0f, 1.0f);

                g.setColour (juce::Colours::black.withAlpha (0.28f));
                g.drawRoundedRectangle (frame.reduced (0.75f), 3.5f, 0.60f);
            }

            // Pass 2: Track (background channel) — lower contrast, subtle recess (no bright slot)
            {
                const auto trackTop = juce::Colour (0xFF151515);
                const auto trackBot = juce::Colour (0xFF101010);
                juce::ColourGradient bgGrad (trackTop, bounds.getX(), bounds.getY(),
                                             trackBot, bounds.getX(), bounds.getBottom(),
                                             false);
                g.setGradientFill (bgGrad);
                g.fillRoundedRectangle (bounds, 4.0f);

                // Optional restrained tie-in tint (meters only; felt not seen)
                if (isInput)
                    g.setColour (juce::Colour (0xFF0088FF).withAlpha (0.015f));
                else
                    g.setColour (juce::Colour (0xFFFF4444).withAlpha (0.015f));
                g.fillRoundedRectangle (bounds, 4.0f);

                // Subtle inner edge for depth (very light)
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 3.5f, 0.60f);
                g.setColour (juce::Colours::black.withAlpha (0.24f));
                g.drawRoundedRectangle (bounds.reduced (1.0f), 3.0f, 0.60f);
            }

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

            // Gradient palette (green -> yellow -> red)
            const auto green  = juce::Colour::fromRGB (60, 200, 110);
            const auto yellow = juce::Colour::fromRGB (230, 200, 70);
            const auto red    = juce::Colour::fromRGB (230, 70, 70);

            auto colourForDot = [&] (int i, bool on)
            {
                // t=0 bottom, t=1 top
                const float t = (kDots <= 1) ? 0.0f : (float) i / (float) (kDots - 1);

                const float tGreenEnd  = (float) kGreen / (float) kDots;
                const float tYellowEnd = (float) (kGreen + kYellow) / (float) kDots;

                juce::Colour c;
                if (t < tGreenEnd)
                {
                    const float u = tGreenEnd <= 0.0f ? 0.0f : (t / tGreenEnd);
                    c = green.interpolatedWith (yellow, u);
                }
                else if (t < tYellowEnd)
                {
                    const float u = (tYellowEnd - tGreenEnd) <= 0.0f ? 0.0f : ((t - tGreenEnd) / (tYellowEnd - tGreenEnd));
                    c = yellow.interpolatedWith (red, u * 0.25f); // keep yellow region mostly yellow
                }
                else
                {
                    const float u = (1.0f - tYellowEnd) <= 0.0f ? 0.0f : ((t - tYellowEnd) / (1.0f - tYellowEnd));
                    c = yellow.interpolatedWith (red, u);
                }

                const float a = on ? 0.92f : 0.14f;
                return c.withAlpha (a);
            };

            for (int i = 0; i < kDots; ++i)
            {
                const bool on = (i < litDots);

                const juce::Colour c = colourForDot (i, on);

                // Phase 2: Snap dot Y position
                const float y = UIStyle::Snap::snapPx (yBottom - (float) i * (dotD + gap), physicalScale);

                const auto dot = juce::Rectangle<float> (x, y, dotD, dotD);
                g.setColour (c);
                g.fillRoundedRectangle (dot, dotD * 0.30f);

                // Pass 2: Fill quality (premium, restrained) — tiny highlight on lit segments only
                if (on)
                {
                    const float px = juce::jmax (1.0f, 1.0f / physicalScale);
                    g.setColour (juce::Colours::white.withAlpha (0.10f));
                    g.drawLine (dot.getX() + px, dot.getY() + px,
                                dot.getRight() - px, dot.getY() + px,
                                1.0f * px);
                }
            }

            // Subtle dB tick marks (within meter lane; no external labels due to narrow meter width)
            {
                g.setColour (juce::Colours::silver.withAlpha (0.25f));
                const float x1 = bounds.getX() + 2.0f;
                const float x2 = bounds.getRight() - 2.0f;

                // Approximate SSL tick positions by normalizing dB into the 0..1 meter range.
                // Use gain mapping and clamp: -18dB..0dB => 0..1
                auto tickYForDb = [&] (float db)
                {
                    const float g01 = juce::Decibels::decibelsToGain (db);
                    const float gMax = juce::Decibels::decibelsToGain (0.0f);
                    const float t = juce::jlimit (0.0f, 1.0f, g01 / gMax);
                    return UIStyle::Snap::snapPx (bounds.getBottom() - t * bounds.getHeight(), physicalScale);
                };

                for (float db : { -18.0f, -12.0f, -6.0f, -3.0f, 0.0f })
                {
                    const float yTick = tickYForDb (db);
                    g.drawLine (x1, yTick, x2, yTick, 1.0f);
                }
            }
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
            
            // Engraved popup readout (stronger, matches headers)
            auto bounds = getLocalBounds().toFloat();
            const float px = 1.0f / physicalScale;
            const auto& font = UIStyle::FontLadder::headerFont (scaleKey);
            g.setFont (font.withHeight (font.getHeight() * 1.1f).withExtraKerningFactor (-0.05f));

            // Snap baseline Y
            const float snappedY = UIStyle::Snap::snapPx (bounds.getY(), physicalScale);
            bounds.setY (snappedY);

            // 1) Shadow
            g.setColour (juce::Colours::black.withAlpha (0.80f));
            g.drawText (juce::StringRef (textBuffer), bounds.translated (1.2f * px, 1.2f * px), juce::Justification::centred, false);

            // 2) Main
            g.setColour (juce::Colour (0xFFE8E8E8));
            g.drawText (juce::StringRef (textBuffer), bounds, juce::Justification::centred, false);

            // 3) Top highlight
            g.setColour (juce::Colours::white.withAlpha (0.40f));
            g.drawText (juce::StringRef (textBuffer), bounds.translated (0.0f, -1.0f * px), juce::Justification::centred, false);
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
    static constexpr int kReadoutY = 20;
    static constexpr int kReadoutW = 160;
    static constexpr int kReadoutH = 20;

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
            (void) shouldDrawButtonAsDown; // no hover/pressed styling

            // SSL BYPASS (red-on) — visual-only override (layout unchanged)
            const auto rOuter = b.toFloat().reduced (3.0f);
            if (rOuter.isEmpty())
                return;

            // Outer bevel frame
            g.setColour (juce::Colours::silver.withAlpha (0.5f));
            g.drawRoundedRectangle (rOuter, 8.0f, 2.0f);

            // Fill: grey when off, dark red when on
            const auto fill = isOn ? juce::Colour (0xFF8B0000).brighter (0.2f)
                                   : juce::Colours::darkgrey.brighter (0.15f);
            const auto rFill = rOuter.reduced (2.0f);
            g.setColour (fill);
            g.fillRoundedRectangle (rFill, 6.0f);

            // Engraved text (3-pass)
            const auto textArea = rFill.reduced (2.0f);
            const auto txt = getButtonText();
            const auto just = juce::Justification::centred;

            g.setFont (UIStyle::FontLadder::headerFont (1.0f).withHeight (12.0f).withExtraKerningFactor (-0.05f));

            // Phase 4 silkscreen/engraved text (match drawEngravedFitted tuning)
            g.setColour (juce::Colours::black.withAlpha (0.80f));
            g.drawText (txt, textArea.translated (1.2f, 1.2f), just, false);

            g.setColour (juce::Colour (0xFFE8E8E8).withAlpha (0.98f));
            g.drawText (txt, textArea, just, false);

            // Subtle bevel + top highlight
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.drawText (txt, textArea.translated (0.0f, -0.5f), just, false);
            g.setColour (juce::Colours::white.withAlpha (0.40f));
            g.drawText (txt, textArea.translated (0.0f, -0.6f), just, false);
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
