#include "PluginEditor.h"
#include "Phase1Spec.h"
#include "UIStyle.h"

static constexpr int kEditorW = 760;
static constexpr int kEditorH = 420;

// ===== Phase 5.0 helper (cpp-only) =====
// ===== Phase 6.0 audit overlay (OFF by default) =====
static constexpr int kPaintAuditOverlay = 0;

namespace
{
    struct PlateStyle
    {
        float fillA      = 0.05f;
        float strokeA    = 0.12f;
        float strokeW    = 1.0f;
        float radius     = 6.0f;
        int   insetPx    = 0;   // optional: shrink rect for visual breathing
    };

    static inline void drawPlate (juce::Graphics& g, juce::Rectangle<int> r, PlateStyle s)
    {
        if (r.isEmpty())
            return;

        if (s.insetPx > 0)
            r = r.reduced (s.insetPx);

        const auto rf = r.toFloat();

        g.setColour (UIStyle::Colors::foreground.withAlpha (s.fillA));
        g.fillRoundedRectangle (rf, s.radius);

        g.setColour (UIStyle::Colors::foreground.withAlpha (s.strokeA));
        g.drawRoundedRectangle (rf, s.radius, s.strokeW);
    }

    static inline juce::Rectangle<int> fullWidthFrom (juce::Rectangle<int> editor, juce::Rectangle<int> zone, int inset)
    {
        // "Derived from slots": we take Y/H from the slot zone, and X/W from the editor bounds.
        if (zone.isEmpty() || editor.isEmpty())
            return {};

        auto r = juce::Rectangle<int> (editor.getX() + inset, zone.getY(), editor.getWidth() - (inset * 2), zone.getHeight());
        return r.getIntersection (editor);
    }
}

// ===== Value popup helper (cpp-only) =====
static inline juce::String popupTextFor (juce::Slider& s)
{
    // Uses JUCE's value→text conversion (suffix/decimals) if you set it.
    return s.getTextFromValue (s.getValue());
}

// ===== Custom LookAndFeel implementation =====
void CompassEQAudioProcessorEditor::CompassLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto centre = bounds.getCentre();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // Snap centre to pixel grid for anti-alias stability
    const auto cx = std::round (centre.x);
    const auto cy = std::round (centre.y);
    const auto r = radius;

    // A) Base matte body (flat, no gradient hotspot)
    g.setColour (UIStyle::Colors::knobBody);
    g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

    // B) Bottom occlusion (depth cue - clipped)
    g.saveState();
    juce::Path knobClip;
    knobClip.addEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.reduceClipRegion (knobClip);
    
    juce::ColourGradient bottomOcclusion (juce::Colours::transparentBlack, cx, cy + r * UIStyle::Knob::occlusionTopOffset,
                                         UIStyle::Colors::knobOcclusion.withAlpha (UIStyle::Knob::occlusionAlpha), cx, cy + r * UIStyle::Knob::occlusionBottomOffset, false);
    g.setGradientFill (bottomOcclusion);
    g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.restoreState();

    // C) Hardware rings (readability)
    // 1. Outer silhouette ring
    const auto outerRimThickness = UIStyle::Knob::getOuterRimThickness (r);
    g.setColour (UIStyle::Colors::knobOuterRim.withAlpha (UIStyle::Knob::outerRimAlpha));
    g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, outerRimThickness);

    // 2. Lip highlight ring (just inside silhouette)
    const auto lipRadius = r * UIStyle::Knob::lipRadiusMultiplier;
    const auto lipThickness = UIStyle::Knob::getLipThickness (r);
    g.setColour (UIStyle::Colors::knobLipHighlight.withAlpha (UIStyle::Knob::lipHighlightAlpha));
    g.drawEllipse (cx - lipRadius, cy - lipRadius, lipRadius * 2.0f, lipRadius * 2.0f, lipThickness);

    // 3. Inner shadow ring
    const auto innerShadowRadius = r * UIStyle::Knob::innerShadowRadiusMultiplier;
    const auto innerShadowThickness = UIStyle::Knob::getInnerShadowThickness (r);
    g.setColour (UIStyle::Colors::knobInnerShadow.withAlpha (UIStyle::Knob::innerShadowAlpha));
    g.drawEllipse (cx - innerShadowRadius, cy - innerShadowRadius, innerShadowRadius * 2.0f, innerShadowRadius * 2.0f, innerShadowThickness);

    // D) Indicator line
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto lineLength = r * UIStyle::Knob::indicatorLengthMultiplier;
    const auto lineThickness = UIStyle::Knob::getIndicatorThickness (r);

    const auto lineStart = juce::Point<float> (cx, cy).getPointOnCircumference (r * UIStyle::Knob::indicatorStartRadiusMultiplier, angle);
    const auto lineEnd = juce::Point<float> (cx, cy).getPointOnCircumference (lineLength, angle);

    // Under-stroke (slightly thicker than main line, lower alpha)
    const auto underStrokeThickness = UIStyle::Knob::getIndicatorUnderStrokeThickness (lineThickness);
    g.setColour (UIStyle::Colors::knobIndicatorUnderStroke.withAlpha (UIStyle::Knob::indicatorUnderStrokeAlpha));
    juce::Path underStroke;
    underStroke.startNewSubPath (lineStart);
    underStroke.lineTo (lineEnd);
    g.strokePath (underStroke, juce::PathStrokeType (underStrokeThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Main indicator line
    g.setColour (UIStyle::Colors::knobIndicator);
    juce::Path indicatorPath;
    indicatorPath.startNewSubPath (lineStart);
    indicatorPath.lineTo (lineEnd);
    g.strokePath (indicatorPath, juce::PathStrokeType (lineThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

CompassEQAudioProcessorEditor::CompassEQAudioProcessorEditor (CompassEQAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
    , proc (p)
    , apvts (proc.getAPVTS())
    , inputMeter  (proc, true)
    , outputMeter (proc, false)
    , lookAndFeel (std::make_unique<CompassLookAndFeel>())
{
    setResizable (false, false);
    setSize (kEditorW, kEditorH);

    // Configure knobs (standard JUCE rotary; no popups/tooltips/overlays)
    configureKnob (lfFreq); configureKnob (lfGain);
    configureKnob (lmfFreq); configureKnob (lmfGain); configureKnob (lmfQ);
    configureKnob (hmfFreq); configureKnob (hmfGain); configureKnob (hmfQ);
    configureKnob (hfFreq);  configureKnob (hfGain);

    configureKnob (hpfFreq);
    configureKnob (lpfFreq);

    configureKnob (inTrim);
    configureKnob (outTrim);

    // Internal names (no extra UI elements)
    lfFreq.setName ("LF Frequency");  lfGain.setName ("LF Gain");
    lmfFreq.setName ("LMF Frequency"); lmfGain.setName ("LMF Gain"); lmfQ.setName ("LMF Q");
    hmfFreq.setName ("HMF Frequency"); hmfGain.setName ("HMF Gain"); hmfQ.setName ("HMF Q");
    hfFreq.setName ("HF Frequency");  hfGain.setName ("HF Gain");

    hpfFreq.setName ("HPF Frequency");
    lpfFreq.setName ("LPF Frequency");

    inTrim.setName ("Input Trim");
    outTrim.setName ("Output Trim");

    // ===== Value popup wiring (per-slider) =====
    lfFreq.onDragStart = [this]
    {
        activeSlider = &lfFreq;
        valuePopup.setVisible (true);
    };

    lfFreq.onValueChange = [this]
    {
        activeSlider = &lfFreq;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (lfFreq), juce::dontSendNotification);

        auto r = lfFreq.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    lfFreq.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    lfGain.onDragStart = [this]
    {
        activeSlider = &lfGain;
        valuePopup.setVisible (true);
    };

    lfGain.onValueChange = [this]
    {
        activeSlider = &lfGain;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (lfGain), juce::dontSendNotification);

        auto r = lfGain.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    lfGain.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    lmfFreq.onDragStart = [this]
    {
        activeSlider = &lmfFreq;
        valuePopup.setVisible (true);
    };

    lmfFreq.onValueChange = [this]
    {
        activeSlider = &lmfFreq;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (lmfFreq), juce::dontSendNotification);

        auto r = lmfFreq.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    lmfFreq.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    lmfGain.onDragStart = [this]
    {
        activeSlider = &lmfGain;
        valuePopup.setVisible (true);
    };

    lmfGain.onValueChange = [this]
    {
        activeSlider = &lmfGain;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (lmfGain), juce::dontSendNotification);

        auto r = lmfGain.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    lmfGain.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    lmfQ.onDragStart = [this]
    {
        activeSlider = &lmfQ;
        valuePopup.setVisible (true);
    };

    lmfQ.onValueChange = [this]
    {
        activeSlider = &lmfQ;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (lmfQ), juce::dontSendNotification);

        auto r = lmfQ.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    lmfQ.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    hmfFreq.onDragStart = [this]
    {
        activeSlider = &hmfFreq;
        valuePopup.setVisible (true);
    };

    hmfFreq.onValueChange = [this]
    {
        activeSlider = &hmfFreq;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (hmfFreq), juce::dontSendNotification);

        auto r = hmfFreq.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    hmfFreq.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    hmfGain.onDragStart = [this]
    {
        activeSlider = &hmfGain;
        valuePopup.setVisible (true);
    };

    hmfGain.onValueChange = [this]
    {
        activeSlider = &hmfGain;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (hmfGain), juce::dontSendNotification);

        auto r = hmfGain.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    hmfGain.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    hmfQ.onDragStart = [this]
    {
        activeSlider = &hmfQ;
        valuePopup.setVisible (true);
    };

    hmfQ.onValueChange = [this]
    {
        activeSlider = &hmfQ;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (hmfQ), juce::dontSendNotification);

        auto r = hmfQ.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    hmfQ.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    hfFreq.onDragStart = [this]
    {
        activeSlider = &hfFreq;
        valuePopup.setVisible (true);
    };

    hfFreq.onValueChange = [this]
    {
        activeSlider = &hfFreq;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (hfFreq), juce::dontSendNotification);

        auto r = hfFreq.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    hfFreq.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    hfGain.onDragStart = [this]
    {
        activeSlider = &hfGain;
        valuePopup.setVisible (true);
    };

    hfGain.onValueChange = [this]
    {
        activeSlider = &hfGain;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (hfGain), juce::dontSendNotification);

        auto r = hfGain.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    hfGain.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    hpfFreq.onDragStart = [this]
    {
        activeSlider = &hpfFreq;
        valuePopup.setVisible (true);
    };

    hpfFreq.onValueChange = [this]
    {
        activeSlider = &hpfFreq;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (hpfFreq), juce::dontSendNotification);

        auto r = hpfFreq.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    hpfFreq.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    lpfFreq.onDragStart = [this]
    {
        activeSlider = &lpfFreq;
        valuePopup.setVisible (true);
    };

    lpfFreq.onValueChange = [this]
    {
        activeSlider = &lpfFreq;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (lpfFreq), juce::dontSendNotification);

        auto r = lpfFreq.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    lpfFreq.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    inTrim.onDragStart = [this]
    {
        activeSlider = &inTrim;
        valuePopup.setVisible (true);
    };

    inTrim.onValueChange = [this]
    {
        activeSlider = &inTrim;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (inTrim), juce::dontSendNotification);

        auto r = inTrim.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    inTrim.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };

    outTrim.onDragStart = [this]
    {
        activeSlider = &outTrim;
        valuePopup.setVisible (true);
    };

    outTrim.onValueChange = [this]
    {
        activeSlider = &outTrim;
        valuePopup.setVisible (true);

        valuePopup.setText (popupTextFor (outTrim), juce::dontSendNotification);

        auto r = outTrim.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
    };

    outTrim.onDragEnd = [this]
    {
        valuePopup.setVisible (false);
        activeSlider = nullptr;
    };
    // ===== End value popup wiring =====

    globalBypass.setName ("Global Bypass");

    // Global bypass button (no hidden interactions)
    globalBypass.setButtonText ("BYPASS");
    globalBypass.setClickingTogglesState (true);

    globalBypass.onAltClick = [this]
    {
        proc.togglePureMode();

       #if JUCE_DEBUG
        DBG (juce::String ("[UI] Pure Mode = ") + (proc.getPureMode() ? "ON" : "OFF"));
       #endif
    };

    addAndMakeVisible (globalBypass);

    auto addKnob = [this] (juce::Slider& s) { addAndMakeVisible (s); };

    // Add sliders
    addKnob (lfFreq); addKnob (lfGain);
    addKnob (lmfFreq); addKnob (lmfGain); addKnob (lmfQ);
    addKnob (hmfFreq); addKnob (hmfGain); addKnob (hmfQ);
    addKnob (hfFreq);  addKnob (hfGain);

    addKnob (hpfFreq);
    addKnob (lpfFreq);

    addKnob (inTrim);
    addKnob (outTrim);

    // Meters (2 only)
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    // Value popup label (ensure it exists, is non-interactive, and stays above)
    addAndMakeVisible (valuePopup);
    valuePopup.setVisible (false);
    valuePopup.setJustificationType (juce::Justification::centred);
    valuePopup.setInterceptsMouseClicks (false, false);
    valuePopup.toFront (false);

    // Attachments using REAL IDs from Phase1Spec.h (namespace phase1)
    attLfFreq  = std::make_unique<SliderAttachment> (apvts, phase1::LF_FREQUENCY_ID,  lfFreq);
    attLfGain  = std::make_unique<SliderAttachment> (apvts, phase1::LF_GAIN_ID,       lfGain);

    attLmfFreq = std::make_unique<SliderAttachment> (apvts, phase1::LMF_FREQUENCY_ID, lmfFreq);
    attLmfGain = std::make_unique<SliderAttachment> (apvts, phase1::LMF_GAIN_ID,      lmfGain);
    attLmfQ    = std::make_unique<SliderAttachment> (apvts, phase1::LMF_Q_ID,         lmfQ);

    attHmfFreq = std::make_unique<SliderAttachment> (apvts, phase1::HMF_FREQUENCY_ID, hmfFreq);
    attHmfGain = std::make_unique<SliderAttachment> (apvts, phase1::HMF_GAIN_ID,      hmfGain);
    attHmfQ    = std::make_unique<SliderAttachment> (apvts, phase1::HMF_Q_ID,         hmfQ);

    attHfFreq  = std::make_unique<SliderAttachment> (apvts, phase1::HF_FREQUENCY_ID,  hfFreq);
    attHfGain  = std::make_unique<SliderAttachment> (apvts, phase1::HF_GAIN_ID,       hfGain);

    attHpfFreq = std::make_unique<SliderAttachment> (apvts, phase1::HPF_FREQUENCY_ID, hpfFreq);
    attLpfFreq = std::make_unique<SliderAttachment> (apvts, phase1::LPF_FREQUENCY_ID, lpfFreq);

    attInTrim  = std::make_unique<SliderAttachment> (apvts, phase1::INPUT_TRIM_ID,    inTrim);
    attOutTrim = std::make_unique<SliderAttachment> (apvts, phase1::OUTPUT_TRIM_ID,   outTrim);

    attBypass  = std::make_unique<ButtonAttachment> (apvts, phase1::GLOBAL_BYPASS_ID, globalBypass);
}

CompassEQAudioProcessorEditor::~CompassEQAudioProcessorEditor()
{
    // Clear LookAndFeel from all sliders to prevent crash on destruction
    lfFreq.setLookAndFeel (nullptr);
    lfGain.setLookAndFeel (nullptr);
    lmfFreq.setLookAndFeel (nullptr);
    lmfGain.setLookAndFeel (nullptr);
    lmfQ.setLookAndFeel (nullptr);
    hmfFreq.setLookAndFeel (nullptr);
    hmfGain.setLookAndFeel (nullptr);
    hmfQ.setLookAndFeel (nullptr);
    hfFreq.setLookAndFeel (nullptr);
    hfGain.setLookAndFeel (nullptr);
    hpfFreq.setLookAndFeel (nullptr);
    lpfFreq.setLookAndFeel (nullptr);
    inTrim.setLookAndFeel (nullptr);
    outTrim.setLookAndFeel (nullptr);
}

void CompassEQAudioProcessorEditor::configureKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setPopupDisplayEnabled (false, false, this);
    s.setDoubleClickReturnValue (false, 0.0);
    s.setLookAndFeel (lookAndFeel.get());
}

void CompassEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // ===== Phase 1: Scale Source of Truth + scaleKey policy =====
    // Derive physical pixel scale from active editor paint graphics context
    const auto physicalScale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    physicalScaleLastPaint = physicalScale;
    
    // Compute rawKey = round(physicalScale * 100) / 100
    const float rawKey = std::round (physicalScale * 100.0f) / 100.0f;
    
    // macOS snap-to-known-values (tolerance 0.02)
    float scaleKey;
    if (std::abs (rawKey - 2.00f) <= 0.02f)
        scaleKey = 2.00f;
    else if (std::abs (rawKey - 1.00f) <= 0.02f)
        scaleKey = 1.00f;
    else
        scaleKey = rawKey;
    
    // Stability window: add to history
    scaleKeyHistory[scaleKeyHistoryIndex] = scaleKey;
    scaleKeyHistoryIndex = (scaleKeyHistoryIndex + 1) % stabilityWindowSize;
    if (scaleKeyHistoryCount < stabilityWindowSize)
        scaleKeyHistoryCount++;
    
    // Check if all last N values match (stability requirement)
    // Note: scaleKeyHistoryIndex points to where NEXT value will go
    bool isStable = (scaleKeyHistoryCount >= stabilityWindowSize);
    if (isStable)
    {
        // Get the most recent value (the one we just added, which is at index-1)
        const int mostRecentIdx = (scaleKeyHistoryIndex - 1 + stabilityWindowSize) % stabilityWindowSize;
        const float mostRecent = scaleKeyHistory[mostRecentIdx];
        
        // Check that the last N values (in chronological order) all match
        for (int i = 0; i < stabilityWindowSize; ++i)
        {
            const int idx = (mostRecentIdx - i + stabilityWindowSize) % stabilityWindowSize;
            if (std::abs (scaleKeyHistory[idx] - mostRecent) > 0.001f)
            {
                isStable = false;
                break;
            }
        }
    }
    
    // Rate limiting: check if enough time has passed since last change
    const auto currentTime = juce::Time::currentTimeMillis();
    const bool rateLimitOk = (currentTime - lastScaleKeyChangeTime) >= rateLimitMs;
    
    // Update active scaleKey if stable and rate limit allows
    if (isStable && rateLimitOk)
    {
        // All values are the same if stable, use the most recent
        const int mostRecentIdx = (scaleKeyHistoryIndex - 1 + stabilityWindowSize) % stabilityWindowSize;
        const float candidateKey = scaleKeyHistory[mostRecentIdx];
        if (std::abs (candidateKey - scaleKeyActive) > 0.001f)
        {
            scaleKeyActive = candidateKey;
            lastScaleKeyChangeTime = currentTime;
        }
    }
    // During transition: continue using last-valid active scaleKey (already set)
    
    // ===== Phase 5.0 — Asset-Ready Paint Layer (vector-only, no images) =====
    // Only paint changes. Layout is frozen. All drawing driven by assetSlots.

    // ===== PH9.4 — Paint hygiene ladder (no layout change) =====
    constexpr float kTitleA   = UIStyle::TextAlpha::title;
    constexpr float kHeaderA  = UIStyle::TextAlpha::header;
    constexpr float kMicroA   = UIStyle::TextAlpha::micro;
    constexpr float kTickA    = UIStyle::TextAlpha::tick;

    const auto editor = getLocalBounds();
    g.fillAll (UIStyle::Colors::background);

    // ---- Global border (subtle) ----
    g.setColour (UIStyle::Colors::foreground.withAlpha (UIStyle::UIAlpha::globalBorder));
    g.drawRect (editor, 1);

    // ---- Plate styles (alpha ladder) ----
    // Keep everything subtle—this is a future PNG drop-in map.
    PlateStyle bgPlate     { UIStyle::Plate::FillAlpha::background, UIStyle::Plate::StrokeAlpha::background, UIStyle::Plate::strokeWidth, UIStyle::Plate::Radius::background, 0 };
    PlateStyle headerPlate { UIStyle::Plate::FillAlpha::header, UIStyle::Plate::StrokeAlpha::header, UIStyle::Plate::strokeWidth, UIStyle::Plate::Radius::header, 0 };
    PlateStyle zonePlate   { UIStyle::Plate::FillAlpha::zone, UIStyle::Plate::StrokeAlpha::zone, UIStyle::Plate::strokeWidth, UIStyle::Plate::Radius::zone, 0 };
    PlateStyle subPlate    { UIStyle::Plate::FillAlpha::sub, UIStyle::Plate::StrokeAlpha::sub, UIStyle::Plate::strokeWidth, UIStyle::Plate::Radius::sub, 0 };
    PlateStyle wellPlate   { UIStyle::Plate::FillAlpha::well, UIStyle::Plate::StrokeAlpha::well, UIStyle::Plate::strokeWidth, UIStyle::Plate::Radius::well, 0 };

    // ---- Major plates (derived from assetSlots zones) ----
    const int inset = 16; // paint-only breathing room (not layout)
    auto headerFW = fullWidthFrom (assetSlots.editor, assetSlots.headerZone, inset);
    auto filters  = fullWidthFrom (assetSlots.editor, assetSlots.filtersZone, inset);
    auto bands    = fullWidthFrom (assetSlots.editor, assetSlots.bandsZone, inset);
    auto trims    = fullWidthFrom (assetSlots.editor, assetSlots.trimZone, inset);

    // ===== PH9.1 — Protect meter lanes (paint-only) =====
    {
        constexpr int meterW   = 18;
        constexpr int meterPad = 8;
        const int leftCut  = 24 + meterW + meterPad;
        const int rightCut = getWidth() - 24 - meterW - meterPad;

        g.saveState();
        g.reduceClipRegion (juce::Rectangle<int> (leftCut, 0, rightCut - leftCut, getHeight()));

        drawPlate (g, editor.reduced (8), bgPlate);

        drawPlate (g, headerFW, headerPlate);
        drawPlate (g, filters,  zonePlate);
        drawPlate (g, bands,    zonePlate);
        drawPlate (g, trims,    zonePlate);

        // ---- Optional micro separators aligned to plate edges (no new UI elements) ----
        {
            g.setColour (UIStyle::Colors::foreground.withAlpha (UIStyle::UIAlpha::microSeparator));

            // vertical edges of the bands plate (helps future asset snapping)
            if (! bands.isEmpty())
            {
                g.drawLine ((float) bands.getX(),         (float) bands.getY(),    (float) bands.getX(),         (float) bands.getBottom(), 1.0f);
                g.drawLine ((float) bands.getRight() - 1, (float) bands.getY(),    (float) bands.getRight() - 1, (float) bands.getBottom(), 1.0f);
            }

            // horizontal separators between major zones
            if (! filters.isEmpty())
                g.drawLine ((float) filters.getX(), (float) filters.getBottom(), (float) filters.getRight(), (float) filters.getBottom(), 1.0f);

            if (! bands.isEmpty())
                g.drawLine ((float) bands.getX(), (float) bands.getBottom(), (float) bands.getRight(), (float) bands.getBottom(), 1.0f);
        }

        g.restoreState();
    }

    // ---- Sub-plates: meters, bypass, filter wells, column plates ----
    // Meter wells (use actual meter bounds expanded slightly)
    drawPlate (g, assetSlots.inputMeter.expanded (4, 4).getIntersection (editor),  wellPlate);
    drawPlate (g, assetSlots.outputMeter.expanded (4, 4).getIntersection (editor), wellPlate);

    // Bypass plate
    drawPlate (g, assetSlots.bypass.expanded (10, 8).getIntersection (editor), subPlate);

    // Filter wells
    drawPlate (g, assetSlots.hpfKnob.expanded (10, 10).getIntersection (editor), subPlate);
    drawPlate (g, assetSlots.lpfKnob.expanded (10, 10).getIntersection (editor), subPlate);

    // Column plates (union rects already computed in Phase 4.0)
    drawPlate (g, assetSlots.colLF.expanded (14, 14).getIntersection (editor),  subPlate);
    drawPlate (g, assetSlots.colLMF.expanded (14, 14).getIntersection (editor), subPlate);
    drawPlate (g, assetSlots.colHMF.expanded (14, 14).getIntersection (editor), subPlate);
    drawPlate (g, assetSlots.colHF.expanded (14, 14).getIntersection (editor),  subPlate);

    // ---- Keep your existing Phase 3.3 text system (headers/legends/ticks) ----
    // Fonts
    const auto titleFont  = juce::FontOptions (18.0f, juce::Font::bold);
    const auto headerFont = juce::FontOptions (11.0f, juce::Font::bold);
    const auto microFont  = juce::FontOptions (9.0f);

    auto drawHeaderAbove = [&g, &headerFont, kHeaderA] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kHeaderA));
        g.setFont (headerFont);
        g.drawFittedText (txt, b.getX(), b.getY() + yOffset, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawLegendBelow = [&g, &microFont, kMicroA] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kMicroA));
        g.setFont (microFont);
        g.drawFittedText (txt, b.getX(), b.getBottom() + yOffset, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawTick = [&g, kTickA] (juce::Rectangle<int> b, int yOffset)
    {
        const int cx = b.getCentreX();
        const int y0 = b.getY() + yOffset;
        const int y1 = y0 + 6;
        g.setColour (UIStyle::Colors::foreground.withAlpha (kTickA));
        g.drawLine ((float) cx, (float) y0, (float) cx, (float) y1, 1.0f);
    };

    auto drawColLabel = [&g, &headerFont, kHeaderA] (const char* txt, juce::Rectangle<int> columnBounds, int y)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kHeaderA));
        g.setFont (headerFont);
        g.drawFittedText (txt, columnBounds.getX(), y, columnBounds.getWidth(), 14, juce::Justification::centred, 1);
    };

    // Title (centered inside header plate)
    g.setColour (UIStyle::Colors::foreground.withAlpha (kTitleA));
    g.setFont (titleFont);
    if (! headerFW.isEmpty())
        g.drawText ("COMPASS EQ", headerFW.withTrimmedTop (6).withHeight (24), juce::Justification::centred, false);

    // Column labels (driven by slot unions)
    const int topY = juce::jmin (assetSlots.colLF.getY(),
                                assetSlots.colLMF.getY(),
                                assetSlots.colHMF.getY(),
                                assetSlots.colHF.getY());
    const int bandLabelY = topY - 18;

    drawColLabel ("LF",  assetSlots.colLF,  bandLabelY);
    drawColLabel ("LMF", assetSlots.colLMF, bandLabelY);
    drawColLabel ("HMF", assetSlots.colHMF, bandLabelY);
    drawColLabel ("HF",  assetSlots.colHF,  bandLabelY);

    // Headers
    drawHeaderAbove ("HPF", hpfFreq.getBounds(), -16);
    drawHeaderAbove ("LPF", lpfFreq.getBounds(), -16);

    drawHeaderAbove ("IN",  inputMeter.getBounds(),  -16);
    drawHeaderAbove ("OUT", outputMeter.getBounds(), -16);

    drawHeaderAbove ("IN",  inTrim.getBounds(),  -16);
    drawHeaderAbove ("OUT", outTrim.getBounds(), -16);

    // Legends
    drawLegendBelow ("FREQ", lfFreq.getBounds(),  2);
    drawLegendBelow ("GAIN", lfGain.getBounds(),  2);

    drawLegendBelow ("FREQ", lmfFreq.getBounds(), 2);
    drawLegendBelow ("GAIN", lmfGain.getBounds(), 2);
    drawLegendBelow ("Q",    lmfQ.getBounds(),    2);

    drawLegendBelow ("FREQ", hmfFreq.getBounds(), 2);
    drawLegendBelow ("GAIN", hmfGain.getBounds(), 2);
    drawLegendBelow ("Q",    hmfQ.getBounds(),    2);

    drawLegendBelow ("FREQ", hfFreq.getBounds(),  2);
    drawLegendBelow ("GAIN", hfGain.getBounds(),  2);

    drawLegendBelow ("FREQ", hpfFreq.getBounds(), 2);
    drawLegendBelow ("FREQ", lpfFreq.getBounds(), 2);

    drawLegendBelow ("TRIM", inTrim.getBounds(),  2);
    drawLegendBelow ("TRIM", outTrim.getBounds(), 2);

    // Ticks
    drawTick (lfFreq.getBounds(),  -2);  drawTick (lfGain.getBounds(), -2);
    drawTick (lmfFreq.getBounds(), -2);  drawTick (lmfGain.getBounds(), -2); drawTick (lmfQ.getBounds(), -2);
    drawTick (hmfFreq.getBounds(), -2);  drawTick (hmfGain.getBounds(), -2); drawTick (hmfQ.getBounds(), -2);
    drawTick (hfFreq.getBounds(),  -2);  drawTick (hfGain.getBounds(), -2);

    drawTick (hpfFreq.getBounds(), -2);
    drawTick (lpfFreq.getBounds(), -2);

    drawTick (inTrim.getBounds(),  -2);
    drawTick (outTrim.getBounds(), -2);

    // Keep your Phase 4 debug overlay (still OFF by default)
    if constexpr (kAssetSlotDebug == 1)
    {
        auto draw = [&g] (juce::Rectangle<int> r)
        {
            g.setColour (UIStyle::Colors::foreground.withAlpha (UIStyle::UIAlpha::debugOverlay));
            g.drawRect (r, 1);
        };

        draw (assetSlots.headerZone);
        draw (assetSlots.filtersZone);
        draw (assetSlots.bandsZone);
        draw (assetSlots.trimZone);

        draw (assetSlots.colLF);
        draw (assetSlots.colLMF);
        draw (assetSlots.colHMF);
        draw (assetSlots.colHF);
    }

    // ===== Phase 6.0 paint-audit overlay (OFF by default) =====
    if constexpr (kPaintAuditOverlay == 1)
    {
        auto box = [&g] (juce::Rectangle<int> r, float a)
        {
            if (r.isEmpty()) return;
            g.setColour (UIStyle::Colors::foreground.withAlpha (a));
            g.drawRect (r, 1);
        };

        // Asset slots
        box (assetSlots.headerZone, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.filtersZone, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.bandsZone, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.trimZone, UIStyle::UIAlpha::auditOverlay);

        box (assetSlots.colLF, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.colLMF, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.colHMF, UIStyle::UIAlpha::auditOverlay);
        box (assetSlots.colHF, UIStyle::UIAlpha::auditOverlay);

        // Knob bounds (exact control bounds)
        box (lfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (lfGain.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (lmfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (lmfGain.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (lmfQ.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (hmfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (hmfGain.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (hmfQ.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (hfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob); box (hfGain.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);

        box (hpfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (lpfFreq.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);

        box (inTrim.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);
        box (outTrim.getBounds(), UIStyle::UIAlpha::auditOverlayKnob);

        // Meters
        box (inputMeter.getBounds(), UIStyle::UIAlpha::auditOverlayMeter);
        box (outputMeter.getBounds(), UIStyle::UIAlpha::auditOverlayMeter);
    }
}

void CompassEQAudioProcessorEditor::resized()
{
    // ===== Layout Freeze Spec v0.1 (AUTHORITATIVE) =====
    // Editor: 760 x 420
    // Grid base: 8 (all integer placement)
    // Zones: 64 / 72 / 200 / 84 (sum 420)
    // Margins: L/R = 24
    // Columns: LF 160, LMF 168, HMF 168, HF 160
    // Gaps: 19 / 19 / 18 (deterministic)
    // Knobs: Primary 56, Secondary 48, Tertiary 40
    // Meters: 18w, header padding 8 top/bottom

    const int editorW = kEditorW;
    const int editorH = kEditorH;
    (void) editorH;

    const int marginL = 24;
    const int marginR = 24;
    const int usableW = editorW - marginL - marginR; // 712

    // Zone Y positions
    const int z1Y = 0;
    const int z1H = 64;

    const int z2Y = z1Y + z1H;
    const int z2H = 72;

    const int z3Y = z2Y + z2H;
    const int z3H = 200;

    const int z4Y = z3Y + z3H;
    const int z4H = 84;

    // ----- Zone 1: Header (meters) -----
    // ===== PH9.3 — Bottom-anchored meters (bottom -> mid) =====
    {
        constexpr int meterW = 18;

        const int inMeterX  = 24;
        const int outMeterX = getWidth() - 24 - meterW;

        // Bottom anchor: sit above the bottom border, but below trim zone content
        const int meterBottomPad = 12;
        const int meterBottomY   = getHeight() - meterBottomPad;

        // Top target: around the middle of the UI (use the filters/bands boundary as a stable "mid")
        const int midY = z3Y; // filters/bands boundary (stable reference)

        // Small top pad so it doesn't kiss the mid line
        const int meterTopPad = 10;
        const int meterY = midY + meterTopPad;

        const int meterH = juce::jmax (60, meterBottomY - meterY);

        inputMeter.setBounds  (inMeterX,  meterY, meterW, meterH);
        outputMeter.setBounds (outMeterX, meterY, meterW, meterH);
    }

    // ----- Zone 2: Filters (HPF/LPF) -----
    const int filterKnob = 48;
    const int filterSpacing = 32;
    const int filtersTotalW = filterKnob + filterSpacing + filterKnob; // 128

    const int filtersStartX = marginL + ((usableW - filtersTotalW) / 2); // 316
    const int filtersY = z2Y + ((z2H - filterKnob) / 2);                // 76

    hpfFreq.setBounds (filtersStartX,                        filtersY, filterKnob, filterKnob);
    lpfFreq.setBounds (filtersStartX + filterKnob + filterSpacing, filtersY, filterKnob, filterKnob);

    // ----- Zone 3: EQ Bands -----
    // Columns + deterministic gaps: 19 / 19 / 18
    const int gap1 = 19;
    const int gap2 = 19;
    const int gap3 = 18;

    const int lfW  = 160;
    const int lmfW = 168;
    const int hmfW = 168;
    const int hfW  = 160;

    const int lfX  = marginL;
    const int lmfX = lfX  + lfW  + gap1; // 203
    const int hmfX = lmfX + lmfW + gap2; // 390
    const int hfX  = hmfX + hmfW + gap3; // 576

    // Knob sizes
    const int kPrimary   = 56;
    const int kSecondary = 48;
    const int kTertiary  = 40;

    // Zone 3 vertical centering math (integers)
    // LMF/HMF stack: 48 + 16 + 56 + 16 + 40 = 176 => top offset (200-176)/2 = 12
    const int stack3Top = z3Y + 12;
    const int lmfFreqY  = stack3Top;               // 148
    const int lmfGainY  = lmfFreqY + 48 + 16;      // 212
    const int lmfQY     = lmfGainY + 56 + 16;      // 284

    // LF/HF stack: 48 + 20 + 56 = 124 => top offset (200-124)/2 = 38
    const int stack2Top = z3Y + 38;
    const int lfFreqY   = stack2Top;               // 174
    const int lfGainY   = lfFreqY + 48 + 20;       // 242

    auto centerX = [] (int colX, int colW, int knobW) -> int
    {
        return colX + ((colW - knobW) / 2);
    };

    // LF (Freq 48, Gain 56)
    lfFreq.setBounds (centerX (lfX, lfW, kSecondary), lfFreqY, kSecondary, kSecondary);
    lfGain.setBounds (centerX (lfX, lfW, kPrimary),   lfGainY, kPrimary,   kPrimary);

    // LMF (Freq 48, Gain 56, Q 40)
    lmfFreq.setBounds (centerX (lmfX, lmfW, kSecondary), lmfFreqY, kSecondary, kSecondary);
    lmfGain.setBounds (centerX (lmfX, lmfW, kPrimary),   lmfGainY, kPrimary,   kPrimary);
    lmfQ.setBounds    (centerX (lmfX, lmfW, kTertiary),  lmfQY,    kTertiary,  kTertiary);

    // HMF (Freq 48, Gain 56, Q 40)
    hmfFreq.setBounds (centerX (hmfX, hmfW, kSecondary), lmfFreqY, kSecondary, kSecondary);
    hmfGain.setBounds (centerX (hmfX, hmfW, kPrimary),   lmfGainY, kPrimary,   kPrimary);
    hmfQ.setBounds    (centerX (hmfX, hmfW, kTertiary),  lmfQY,    kTertiary,  kTertiary);

    // HF (Freq 48, Gain 56)
    hfFreq.setBounds (centerX (hfX, hfW, kSecondary), lfFreqY, kSecondary, kSecondary);
    hfGain.setBounds (centerX (hfX, hfW, kPrimary),   lfGainY, kPrimary,   kPrimary);

    // ----- Zone 4: Trim + Bypass -----
    // ===== PH9.4 — Zone 4: Center BYPASS + symmetric trims =====
    {
        constexpr int g = 8;

        // Re-derive Zone 4 from editor bounds (no floats, deterministic)
        auto editor = getLocalBounds();
        auto zone4  = editor.removeFromBottom (84).reduced (24, 0); // Zone 4 height per freeze spec

        // Vertical centering in Zone 4
        constexpr int trimSize   = 56;
        constexpr int bypassW    = 140;
        constexpr int bypassH    = 32;

        const int cy = zone4.getCentreY();

        // BYPASS centered
        const auto bypassBounds = juce::Rectangle<int> (0, 0, bypassW, bypassH)
                                    .withCentre ({ zone4.getCentreX(), cy });
        globalBypass.setBounds (bypassBounds);

        // Trims: symmetric around bypass, keep >= 32px spacing (4g)
        constexpr int minGapToBypass = 32;

        const int leftTrimCx  = bypassBounds.getX() - minGapToBypass - (trimSize / 2);
        const int rightTrimCx = bypassBounds.getRight() + minGapToBypass + (trimSize / 2);

        inTrim.setBounds  (juce::Rectangle<int> (0, 0, trimSize, trimSize).withCentre ({ leftTrimCx,  cy }));
        outTrim.setBounds (juce::Rectangle<int> (0, 0, trimSize, trimSize).withCentre ({ rightTrimCx, cy }));
    }

    // ===== Phase 4.0 — Asset Slot Map (derived from existing bounds) =====
    {
        constexpr int g = 8;

        assetSlots = {}; // reset

        assetSlots.editor = getLocalBounds();

        // Exact component bounds
        assetSlots.inputMeter  = inputMeter.getBounds();
        assetSlots.outputMeter = outputMeter.getBounds();

        assetSlots.hpfKnob = hpfFreq.getBounds();
        assetSlots.lpfKnob = lpfFreq.getBounds();

        assetSlots.lfFreq = lfFreq.getBounds();   assetSlots.lfGain = lfGain.getBounds();

        assetSlots.lmfFreq = lmfFreq.getBounds(); assetSlots.lmfGain = lmfGain.getBounds(); assetSlots.lmfQ = lmfQ.getBounds();
        assetSlots.hmfFreq = hmfFreq.getBounds(); assetSlots.hmfGain = hmfGain.getBounds(); assetSlots.hmfQ = hmfQ.getBounds();

        assetSlots.hfFreq = hfFreq.getBounds();   assetSlots.hfGain = hfGain.getBounds();

        assetSlots.inTrim  = inTrim.getBounds();
        assetSlots.outTrim = outTrim.getBounds();
        assetSlots.bypass  = globalBypass.getBounds();

        // Unions (derived only)
        assetSlots.filtersUnion = assetSlots.hpfKnob.getUnion (assetSlots.lpfKnob);

        assetSlots.bandsUnion =
            assetSlots.lfFreq.getUnion (assetSlots.lfGain)
                .getUnion (assetSlots.lmfFreq).getUnion (assetSlots.lmfGain).getUnion (assetSlots.lmfQ)
                .getUnion (assetSlots.hmfFreq).getUnion (assetSlots.hmfGain).getUnion (assetSlots.hmfQ)
                .getUnion (assetSlots.hfFreq).getUnion (assetSlots.hfGain);

        assetSlots.trimsUnion = assetSlots.inTrim.getUnion (assetSlots.outTrim).getUnion (assetSlots.bypass);

        // Column unions (useful for later panel assets)
        assetSlots.colLF  = assetSlots.lfFreq.getUnion (assetSlots.lfGain);
        assetSlots.colLMF = assetSlots.lmfFreq.getUnion (assetSlots.lmfGain).getUnion (assetSlots.lmfQ);
        assetSlots.colHMF = assetSlots.hmfFreq.getUnion (assetSlots.hmfGain).getUnion (assetSlots.hmfQ);
        assetSlots.colHF  = assetSlots.hfFreq.getUnion (assetSlots.hfGain);

        // Major zones (derived from component bounds, expanded by grid)
        assetSlots.headerZone  = assetSlots.inputMeter.getUnion (assetSlots.outputMeter).expanded (g, g);
        assetSlots.filtersZone = assetSlots.filtersUnion.expanded (g * 2, g * 2);
        assetSlots.bandsZone   = assetSlots.bandsUnion.expanded (g * 2, g * 2);
        assetSlots.trimZone    = assetSlots.trimsUnion.expanded (g * 2, g * 2);

        // Clamp to editor
        auto clamp = [&] (juce::Rectangle<int>& r)
        {
            r = r.getIntersection (assetSlots.editor);
        };

        clamp (assetSlots.headerZone);
        clamp (assetSlots.filtersZone);
        clamp (assetSlots.bandsZone);
        clamp (assetSlots.trimZone);
    }
}
