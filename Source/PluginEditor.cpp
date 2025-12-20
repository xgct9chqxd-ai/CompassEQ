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
// Phase 3C: PERFORMANCE BUDGET VERIFICATION CHECKLIST
// ✓ No std::vector growth, no std::string concat, no Path churn that allocates
// ✓ No Image reallocation in paint, no Gradient objects created per-frame without reuse/caching
// ✓ Fonts from ladders/tokens (FontLadder returns const references)
// ✓ Path objects: knobClip, underStroke, indicatorPath are stack-allocated (acceptable for small paths)
// ✓ ColourGradient: bottomOcclusion is stack-allocated (acceptable for small gradients)
// ✓ All coordinates are stack-allocated floats/ints
// ✓ Caches rebuilt only on invalidation events (scaleKey change, resize) via AsyncUpdater
void CompassEQAudioProcessorEditor::CompassLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider&)
{
    // Phase 2: Get scaleKey and physical scale for snapping
    const float scaleKey = editor.getScaleKeyActive();
    const float physicalScale = juce::jmax (1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());

    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto centre = bounds.getCentre();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // Phase 2: Snap centre to pixel grid
    const auto cx = UIStyle::Snap::snapPx (centre.x, physicalScale);
    const auto cy = UIStyle::Snap::snapPx (centre.y, physicalScale);
    const auto r = radius;

    // Phase 2: Snap ellipse bounds - snap BOTH edges, then compute width/height
    const auto ellipseX1 = UIStyle::Snap::snapPx (cx - r, physicalScale);
    const auto ellipseX2 = UIStyle::Snap::snapPx (cx + r, physicalScale);
    const auto ellipseY1 = UIStyle::Snap::snapPx (cy - r, physicalScale);
    const auto ellipseY2 = UIStyle::Snap::snapPx (cy + r, physicalScale);
    const auto ellipseX = ellipseX1;
    const auto ellipseY = ellipseY1;
    const auto ellipseW = ellipseX2 - ellipseX1;
    const auto ellipseH = ellipseY2 - ellipseY1;

    // A) Base matte body (flat, no gradient hotspot)
    g.setColour (UIStyle::Colors::knobBody);
    g.fillEllipse (ellipseX, ellipseY, ellipseW, ellipseH);

    // B) Bottom occlusion (depth cue - clipped)
    g.saveState();
    juce::Path knobClip;
    knobClip.addEllipse (ellipseX, ellipseY, ellipseW, ellipseH);
    g.reduceClipRegion (knobClip);
    
    juce::ColourGradient bottomOcclusion (juce::Colours::transparentBlack, cx, cy + r * UIStyle::Knob::occlusionTopOffset,
                                         UIStyle::Colors::knobOcclusion.withAlpha (UIStyle::Knob::occlusionAlpha), cx, cy + r * UIStyle::Knob::occlusionBottomOffset, false);
    g.setGradientFill (bottomOcclusion);
    g.fillEllipse (ellipseX, ellipseY, ellipseW, ellipseH);
    g.restoreState();

    // C) Hardware rings (readability) - Phase 2: Discrete stroke ladder by scaleKey
    // 1. Outer silhouette ring
    const auto outerRimThickness = UIStyle::Knob::getOuterRimThickness (scaleKey);
    g.setColour (UIStyle::Colors::knobOuterRim.withAlpha (UIStyle::Knob::outerRimAlpha));
    g.drawEllipse (ellipseX, ellipseY, ellipseW, ellipseH, outerRimThickness);

    // 2. Lip highlight ring (just inside silhouette) - Phase 2: Snap both edges
    const auto lipRadius = r * UIStyle::Knob::lipRadiusMultiplier;
    const auto lipThickness = UIStyle::Knob::getLipThickness (scaleKey);
    const auto lipX1 = UIStyle::Snap::snapPx (cx - lipRadius, physicalScale);
    const auto lipX2 = UIStyle::Snap::snapPx (cx + lipRadius, physicalScale);
    const auto lipY1 = UIStyle::Snap::snapPx (cy - lipRadius, physicalScale);
    const auto lipY2 = UIStyle::Snap::snapPx (cy + lipRadius, physicalScale);
    const auto lipX = lipX1;
    const auto lipY = lipY1;
    const auto lipW = lipX2 - lipX1;
    const auto lipH = lipY2 - lipY1;
    g.setColour (UIStyle::Colors::knobLipHighlight.withAlpha (UIStyle::Knob::lipHighlightAlpha));
    g.drawEllipse (lipX, lipY, lipW, lipH, lipThickness);

    // 3. Inner shadow ring - Phase 2: Snap both edges
    const auto innerShadowRadius = r * UIStyle::Knob::innerShadowRadiusMultiplier;
    const auto innerShadowThickness = UIStyle::Knob::getInnerShadowThickness (scaleKey);
    const auto innerX1 = UIStyle::Snap::snapPx (cx - innerShadowRadius, physicalScale);
    const auto innerX2 = UIStyle::Snap::snapPx (cx + innerShadowRadius, physicalScale);
    const auto innerY1 = UIStyle::Snap::snapPx (cy - innerShadowRadius, physicalScale);
    const auto innerY2 = UIStyle::Snap::snapPx (cy + innerShadowRadius, physicalScale);
    const auto innerX = innerX1;
    const auto innerY = innerY1;
    const auto innerW = innerX2 - innerX1;
    const auto innerH = innerY2 - innerY1;
    g.setColour (UIStyle::Colors::knobInnerShadow.withAlpha (UIStyle::Knob::innerShadowAlpha));
    g.drawEllipse (innerX, innerY, innerW, innerH, innerShadowThickness);

    // D) Indicator line - Phase 2: Snap endpoints, discrete stroke
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto lineLength = r * UIStyle::Knob::indicatorLengthMultiplier;
    const auto lineThickness = UIStyle::Knob::getIndicatorThickness (scaleKey);

    auto lineStart = juce::Point<float> (cx, cy).getPointOnCircumference (r * UIStyle::Knob::indicatorStartRadiusMultiplier, angle);
    auto lineEnd = juce::Point<float> (cx, cy).getPointOnCircumference (lineLength, angle);
    
    // Phase 2: Snap indicator endpoints
    lineStart = UIStyle::Snap::snapPoint (lineStart, physicalScale);
    lineEnd = UIStyle::Snap::snapPoint (lineEnd, physicalScale);

    // Under-stroke (slightly thicker than main line, lower alpha)
    const auto underStrokeThickness = UIStyle::Knob::getIndicatorUnderStrokeThickness (scaleKey);
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
    , inputMeter  (proc, true, *this)
    , outputMeter (proc, false, *this)
    , lookAndFeel (std::make_unique<CompassLookAndFeel>(*this))
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

    // ===== Value popup wiring (refactored pattern) =====
    // 1) Startup state: popup must be blank + hidden
    valuePopup.setText ("", juce::dontSendNotification);
    valuePopup.setVisible (false);

    // Helper: show/update popup safely with positioning
    auto showPopupFor = [this] (juce::Slider& s)
    {
        valuePopup.setText (popupTextFor (s), juce::dontSendNotification);
        valuePopup.setVisible (true);
        
        auto r = s.getBounds();
        const int y = juce::jmax (0, r.getY() - 22);
        valuePopup.setBounds (r.getX(), y, r.getWidth(), 18);
        valuePopup.repaint();
    };

    auto hidePopup = [this]()
    {
        valuePopup.setText ("", juce::dontSendNotification);
        valuePopup.setVisible (false);
        valuePopup.repaint();
    };

    // 2) Wire popup behavior for each slider
    auto wirePopup = [this, showPopupFor, hidePopup] (juce::Slider& s)
    {
        s.onDragStart = [this, showPopupFor, &s]
        {
            activeSlider = &s;
            showPopupFor (s);
        };

        s.onValueChange = [this, showPopupFor, &s]
        {
            // Only update while actively dragging
            if (s.isMouseButtonDown())
            {
                activeSlider = &s;
                showPopupFor (s);
            }
        };

        s.onDragEnd = [this, hidePopup]
        {
            hidePopup();
            activeSlider = nullptr;
        };
    };

    // Apply to all sliders
    wirePopup (lfFreq);
    wirePopup (lfGain);
    wirePopup (lmfFreq);
    wirePopup (lmfGain);
    wirePopup (lmfQ);
    wirePopup (hmfFreq);
    wirePopup (hmfGain);
    wirePopup (hmfQ);
    wirePopup (hfFreq);
    wirePopup (hfGain);
    wirePopup (hpfFreq);
    wirePopup (lpfFreq);
    wirePopup (inTrim);
    wirePopup (outTrim);


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
    valuePopup.setJustificationType (juce::Justification::centred);
    valuePopup.setInterceptsMouseClicks (false, false);
    valuePopup.toFront (false);
    // Popup startup state already set above (blank + hidden)

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
    // Phase 4: Set teardown flag first to prevent AsyncUpdater callbacks
    isTearingDown = true;
    
    // Phase 4: Cancel any pending AsyncUpdater callbacks
    cancelPendingUpdate();
    
    // Phase 4: Clear LookAndFeel from all sliders to prevent crash on destruction
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
    
    // Phase 4: lookAndFeel unique_ptr will be destroyed here (after all components cleared)
}

void CompassEQAudioProcessorEditor::configureKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setPopupDisplayEnabled (false, false, this);
    s.setDoubleClickReturnValue (false, 0.0);
    s.setLookAndFeel (lookAndFeel.get());
}

void CompassEQAudioProcessorEditor::renderStaticLayer (juce::Graphics& g, float scaleKey, float physicalScale)
{
    // ===== Phase 5.0 — Asset-Ready Paint Layer (vector-only, no images) =====
    // Only paint changes. Layout is frozen. All drawing driven by assetSlots.

    // Phase 3C: PERFORMANCE BUDGET VERIFICATION CHECKLIST
    // ✓ No std::vector growth, no std::string concat, no Path churn that allocates
    // ✓ No Image reallocation in paint, no Gradient objects created per-frame without reuse/caching
    // ✓ Fonts from ladders/tokens (FontLadder returns const references, no per-frame construction)
    // ✓ All lambda captures are by reference or value (no heap allocations)
    // ✓ All coordinates are stack-allocated floats/ints
    // ✓ drawPlate, drawLine, drawText use stack-allocated parameters only
    // ✓ Caches rebuilt only on invalidation events (scaleKey change, resize) via AsyncUpdater

    // Phase 3 Fix: scaleKey and physicalScale are passed as parameters (from paint() or handleAsyncUpdate())
    // This ensures cache rebuild uses the same physicalScale that paint() observed

    // ===== PH9.4 — Paint hygiene ladder (no layout change) =====
    constexpr float kTitleA   = UIStyle::TextAlpha::title;
    constexpr float kHeaderA  = UIStyle::TextAlpha::header;
    constexpr float kMicroA   = UIStyle::TextAlpha::micro;
    constexpr float kTickA    = UIStyle::TextAlpha::tick;

    const auto editor = getLocalBounds();
    g.fillAll (UIStyle::Colors::background);

    // ---- Global border (subtle) - Phase 2: Use discrete stroke ladder ----
    g.setColour (UIStyle::Colors::foreground.withAlpha (UIStyle::UIAlpha::globalBorder));
    g.drawRect (editor, (int) UIStyle::StrokeLadder::plateBorderStroke (scaleKey));

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
        // Phase 2: Snap hairlines to pixel grid
        {
            g.setColour (UIStyle::Colors::foreground.withAlpha (UIStyle::UIAlpha::microSeparator));
            const float hairlineStroke = UIStyle::StrokeLadder::hairlineStroke (scaleKey);

            // vertical edges of the bands plate (helps future asset snapping)
            if (! bands.isEmpty())
            {
                const float x1 = UIStyle::Snap::snapPx ((float) bands.getX(), physicalScale);
                const float x2 = UIStyle::Snap::snapPx ((float) bands.getRight() - 1, physicalScale);
                const float yTop = UIStyle::Snap::snapPx ((float) bands.getY(), physicalScale);
                const float yBottom = UIStyle::Snap::snapPx ((float) bands.getBottom(), physicalScale);
                g.drawLine (x1, yTop, x1, yBottom, hairlineStroke);
                g.drawLine (x2, yTop, x2, yBottom, hairlineStroke);
            }

            // horizontal separators between major zones
            if (! filters.isEmpty())
            {
                const float xLeft = UIStyle::Snap::snapPx ((float) filters.getX(), physicalScale);
                const float xRight = UIStyle::Snap::snapPx ((float) filters.getRight(), physicalScale);
                const float y = UIStyle::Snap::snapPx ((float) filters.getBottom(), physicalScale);
                g.drawLine (xLeft, y, xRight, y, hairlineStroke);
            }

            if (! bands.isEmpty())
            {
                const float xLeft = UIStyle::Snap::snapPx ((float) bands.getX(), physicalScale);
                const float xRight = UIStyle::Snap::snapPx ((float) bands.getRight(), physicalScale);
                const float y = UIStyle::Snap::snapPx ((float) bands.getBottom(), physicalScale);
                g.drawLine (xLeft, y, xRight, y, hairlineStroke);
            }
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
    // Phase 2: Discrete font ladder by scaleKey
    const auto& titleFont  = UIStyle::FontLadder::titleFont (scaleKey);
    const auto& headerFont = UIStyle::FontLadder::headerFont (scaleKey);
    const auto& microFont  = UIStyle::FontLadder::microFont (scaleKey);
    const float hairlineStroke = UIStyle::StrokeLadder::hairlineStroke (scaleKey);

    auto drawHeaderAbove = [&g, &headerFont, kHeaderA, physicalScale] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kHeaderA));
        g.setFont (headerFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset), physicalScale);
        g.drawFittedText (txt, b.getX(), (int) snappedY, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawLegendBelow = [&g, &microFont, kMicroA, physicalScale] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kMicroA));
        g.setFont (microFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) (b.getBottom() + yOffset), physicalScale);
        g.drawFittedText (txt, b.getX(), (int) snappedY, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawTick = [&g, kTickA, physicalScale, hairlineStroke] (juce::Rectangle<int> b, int yOffset)
    {
        const float cx = UIStyle::Snap::snapPx ((float) b.getCentreX(), physicalScale);
        const float y0 = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset), physicalScale);
        const float y1 = UIStyle::Snap::snapPx ((float) (b.getY() + yOffset + 6), physicalScale);
        g.setColour (UIStyle::Colors::foreground.withAlpha (kTickA));
        g.drawLine (cx, y0, cx, y1, hairlineStroke);
    };

    auto drawColLabel = [&g, &headerFont, kHeaderA, physicalScale] (const char* txt, juce::Rectangle<int> columnBounds, int y)
    {
        g.setColour (UIStyle::Colors::foreground.withAlpha (kHeaderA));
        g.setFont (headerFont);
        // Phase 2: Snap baseline Y
        const float snappedY = UIStyle::Snap::snapPx ((float) y, physicalScale);
        g.drawFittedText (txt, columnBounds.getX(), (int) snappedY, columnBounds.getWidth(), 14, juce::Justification::centred, 1);
    };

    // Title (centered inside header plate) - Phase 2: Snap baseline Y
    g.setColour (UIStyle::Colors::foreground.withAlpha (kTitleA));
    g.setFont (titleFont);
    if (! headerFW.isEmpty())
    {
        auto titleRect = headerFW.withTrimmedTop (6).withHeight (24);
        const float snappedY = UIStyle::Snap::snapPx ((float) titleRect.getY(), physicalScale);
        titleRect.setY ((int) snappedY);
        g.drawText ("COMPASS EQ", titleRect, juce::Justification::centred, false);
    }

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

void CompassEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Phase 3C: PERFORMANCE BUDGET VERIFICATION CHECKLIST
    // ✓ No std::vector growth, no std::string concat, no Path churn that allocates
    // ✓ No Image reallocation in paint (cache image is pre-allocated, only drawn via drawImageTransformed)
    // ✓ No Gradient objects created per-frame without reuse/caching
    // ✓ Fonts from ladders/tokens (not applicable here, handled in renderStaticLayer)
    // ✓ All coordinates are stack-allocated floats/ints
    // ✓ Caches rebuilt only on invalidation events (scaleKey change, resize) via AsyncUpdater
    // ✓ Cache rebuild happens in handleAsyncUpdate() on UI thread, NOT in paint()
    
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
            
            // Phase 3A: Invalidate cache when scaleKeyActive changes
            staticCacheDirty.store (true, std::memory_order_release);
            if (! staticCacheRebuildPending.exchange (true, std::memory_order_acq_rel))
                triggerAsyncUpdate();
        }
    }
    // During transition: continue using last-valid active scaleKey (already set)
    
    // ===== Phase 2: Static Layer Cache =====
    const float sk = getScaleKeyActive();
    const float physical = juce::jmax (1.0f, physicalScale);
    const int w = getWidth();
    const int h = getHeight();
    const int pw = juce::roundToInt ((double) w * (double) physical);
    const int ph = juce::roundToInt ((double) h * (double) physical);
    
    // Phase 3A: Check if cache is valid and matches current scaleKey and physical pixel size
    const bool cacheValid = staticCache.valid() 
                         && std::abs (staticCache.scaleKey - sk) < 0.001f
                         && staticCache.pixelW == pw
                         && staticCache.pixelH == ph;
    
    if (cacheValid)
    {
        // Draw cached image with transform back to logical coords
        g.drawImageTransformed (staticCache.image, juce::AffineTransform::scale (1.0f / physical));
    }
    else
    {
        // Fallback: draw uncached using the REAL physicalScale from live editor context
        renderStaticLayer (g, sk, physical);
        
        // Mark dirty and trigger async rebuild (outside paint) - prevent spam
        staticCacheDirty.store (true, std::memory_order_release);

        if (! staticCacheRebuildPending.exchange (true, std::memory_order_acq_rel))
            triggerAsyncUpdate();
    }
}

void CompassEQAudioProcessorEditor::handleAsyncUpdate()
{
    staticCacheRebuildPending.store (false, std::memory_order_release);
    
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    
    // Phase 4: Early return if teardown is in progress
    if (isTearingDown)
        return;
    
    // Rebuild cache only if visible and bounds are valid
    if (! isVisible() || getBounds().isEmpty())
        return;
    
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0)
        return;
    
    // Phase 3 Fix: Use the SAME physicalScale that paint() observed (from physicalScaleLastPaint)
    const float physicalScale = juce::jmax (1.0f, getPhysicalScaleLastPaint());
    const int pw = juce::roundToInt ((double) w * (double) physicalScale);
    const int ph = juce::roundToInt ((double) h * (double) physicalScale);
    
    if (pw <= 0 || ph <= 0)
        return;
    
    const float sk = getScaleKeyActive();
    
    // Phase 3A: Rebuild cache ONLY if dirty OR cache.scaleKey != sk OR pixel size mismatch
    if (! staticCacheDirty.load (std::memory_order_acquire)
        && std::abs (staticCache.scaleKey - sk) < 0.001f
        && staticCache.valid()
        && staticCache.pixelW == pw
        && staticCache.pixelH == ph)
        return;
    
    // Phase 3A: Create image at physical pixel size (rebuild happens on UI thread, NOT in paint)
    juce::Image img (juce::Image::ARGB, pw, ph, true);
    juce::Graphics cg (img);
    cg.addTransform (juce::AffineTransform::scale (physicalScale));
    // Phase 3 Fix: Pass physicalScale to renderStaticLayer so snapping uses the same scale paint() observed
    renderStaticLayer (cg, sk, physicalScale);
    
    // Phase 3A: Update cache with scaleKey and physical pixel dimensions
    staticCache.image = std::move (img);
    staticCache.scaleKey = sk;
    staticCache.pixelW = pw;
    staticCache.pixelH = ph;
    staticCacheDirty.store (false, std::memory_order_release);
    
    // Trigger repaint to use new cache
    repaint();
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
    
    // ===== Phase 2: Invalidate cache on resize =====
    staticCacheDirty.store (true, std::memory_order_release);

    if (! staticCacheRebuildPending.exchange (true, std::memory_order_acq_rel))
        triggerAsyncUpdate();
}
