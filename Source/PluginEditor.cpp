#include "PluginEditor.h"
#include "Phase1Spec.h"

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

        g.setColour (juce::Colours::white.withAlpha (s.fillA));
        g.fillRoundedRectangle (rf, s.radius);

        g.setColour (juce::Colours::white.withAlpha (s.strokeA));
        g.drawRoundedRectangle (rf, s.radius, s.strokeW);
    }

    static inline juce::Rectangle<int> fullWidthFrom (juce::Rectangle<int> editor, juce::Rectangle<int> zone, int inset)
    {
        // “Derived from slots”: we take Y/H from the slot zone, and X/W from the editor bounds.
        if (zone.isEmpty() || editor.isEmpty())
            return {};

        auto r = juce::Rectangle<int> (editor.getX() + inset, zone.getY(), editor.getWidth() - (inset * 2), zone.getHeight());
        return r.getIntersection (editor);
    }
}

CompassEQAudioProcessorEditor::CompassEQAudioProcessorEditor (CompassEQAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
    , proc (p)
    , apvts (proc.getAPVTS())
    , inputMeter  (proc, true)
    , outputMeter (proc, false)
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

    globalBypass.setName ("Global Bypass");

    // Global bypass button (no hidden interactions)
    globalBypass.setButtonText ("BYPASS");
    globalBypass.setClickingTogglesState (true);
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

void CompassEQAudioProcessorEditor::configureKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setPopupDisplayEnabled (false, false, this);
    s.setDoubleClickReturnValue (false, 0.0);
}

void CompassEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // ===== Phase 5.0 — Asset-Ready Paint Layer (vector-only, no images) =====
    // Only paint changes. Layout is frozen. All drawing driven by assetSlots.

    const auto editor = getLocalBounds();
    g.fillAll (juce::Colours::black);

    // ---- Global border (subtle) ----
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRect (editor, 1);

    // ---- Plate styles (alpha ladder) ----
    // Keep everything subtle—this is a future PNG drop-in map.
    PlateStyle bgPlate     { 0.015f, 0.07f, 1.0f, 10.0f, 0 };
    PlateStyle headerPlate { 0.030f, 0.10f, 1.0f, 10.0f, 0 };
    PlateStyle zonePlate   { 0.022f, 0.10f, 1.0f,  8.0f, 0 };
    PlateStyle subPlate    { 0.018f, 0.10f, 1.0f,  6.0f, 0 };
    PlateStyle wellPlate   { 0.060f, 0.16f, 1.0f,  4.0f, 0 };

    // ---- Major plates (derived from assetSlots zones) ----
    drawPlate (g, editor.reduced (8), bgPlate);

    const int inset = 16; // paint-only breathing room (not layout)
    auto headerFW = fullWidthFrom (assetSlots.editor, assetSlots.headerZone, inset);
    auto filters  = fullWidthFrom (assetSlots.editor, assetSlots.filtersZone, inset);
    auto bands    = fullWidthFrom (assetSlots.editor, assetSlots.bandsZone, inset);
    auto trims    = fullWidthFrom (assetSlots.editor, assetSlots.trimZone, inset);

    drawPlate (g, headerFW, headerPlate);
    drawPlate (g, filters,  zonePlate);
    drawPlate (g, bands,    zonePlate);
    drawPlate (g, trims,    zonePlate);

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

    // ---- Optional micro separators aligned to plate edges (no new UI elements) ----
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));

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

    // ---- Keep your existing Phase 3.3 text system (headers/legends/ticks) ----
    // Fonts
    const auto titleFont  = juce::FontOptions (18.0f, juce::Font::bold);
    const auto headerFont = juce::FontOptions (11.0f, juce::Font::bold);
    const auto microFont  = juce::FontOptions (9.0f);

    auto drawHeaderAbove = [&g, &headerFont] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (juce::Colours::white.withAlpha (0.70f));
        g.setFont (headerFont);
        g.drawFittedText (txt, b.getX(), b.getY() + yOffset, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawLegendBelow = [&g, &microFont] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (juce::Colours::white.withAlpha (0.42f));
        g.setFont (microFont);
        g.drawFittedText (txt, b.getX(), b.getBottom() + yOffset, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawTick = [&g] (juce::Rectangle<int> b, int yOffset)
    {
        const int cx = b.getCentreX();
        const int y0 = b.getY() + yOffset;
        const int y1 = y0 + 6;
        g.setColour (juce::Colours::white.withAlpha (0.26f));
        g.drawLine ((float) cx, (float) y0, (float) cx, (float) y1, 1.0f);
    };

    auto drawColLabel = [&g, &headerFont] (const char* txt, juce::Rectangle<int> columnBounds, int y)
    {
        g.setColour (juce::Colours::white.withAlpha (0.78f));
        g.setFont (headerFont);
        g.drawFittedText (txt, columnBounds.getX(), y, columnBounds.getWidth(), 14, juce::Justification::centred, 1);
    };

    // Title (centered inside header plate)
    g.setColour (juce::Colours::white.withAlpha (0.88f));
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
            g.setColour (juce::Colours::white.withAlpha (0.20f));
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
            g.setColour (juce::Colours::white.withAlpha (a));
            g.drawRect (r, 1);
        };

        // Asset slots
        box (assetSlots.headerZone, 0.20f);
        box (assetSlots.filtersZone, 0.20f);
        box (assetSlots.bandsZone, 0.20f);
        box (assetSlots.trimZone,   0.20f);

        box (assetSlots.colLF,  0.20f);
        box (assetSlots.colLMF, 0.20f);
        box (assetSlots.colHMF, 0.20f);
        box (assetSlots.colHF,  0.20f);

        // Knob bounds (exact control bounds)
        box (lfFreq.getBounds(),  0.14f); box (lfGain.getBounds(),  0.14f);
        box (lmfFreq.getBounds(), 0.14f); box (lmfGain.getBounds(), 0.14f); box (lmfQ.getBounds(), 0.14f);
        box (hmfFreq.getBounds(), 0.14f); box (hmfGain.getBounds(), 0.14f); box (hmfQ.getBounds(), 0.14f);
        box (hfFreq.getBounds(),  0.14f); box (hfGain.getBounds(),  0.14f);

        box (hpfFreq.getBounds(), 0.14f);
        box (lpfFreq.getBounds(), 0.14f);

        box (inTrim.getBounds(),  0.14f);
        box (outTrim.getBounds(), 0.14f);

        // Meters
        box (inputMeter.getBounds(),  0.18f);
        box (outputMeter.getBounds(), 0.18f);
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
    const int meterW = 18;
    const int meterPadY = 8;
    const int meterH = z1H - (meterPadY * 2); // 48
    const int meterY = z1Y + meterPadY;

    const int inMeterX  = marginL;
    const int outMeterX = editorW - marginR - meterW;

    inputMeter.setBounds  (inMeterX,  meterY, meterW, meterH);
    outputMeter.setBounds (outMeterX, meterY, meterW, meterH);

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
    // Trim knobs are primary 56x56, vertically centered in zone (84 => y = 336 + (84-56)/2 = 350)
    const int trimY = z4Y + ((z4H - kPrimary) / 2); // 350

    // Keep bypass size as currently used (no new control/visual decision introduced here)
    const int bypassW = 140;
    const int bypassH = 32;

    const int minGapToBypass = 32;

    // Center the whole group: [Trim56] gap32 [Bypass140] gap32 [Trim56] = 316
    const int groupW = kPrimary + minGapToBypass + bypassW + minGapToBypass + kPrimary; // 316
    const int groupX = (editorW - groupW) / 2;                                         // 222

    const int inTrimX  = groupX;
    const int bypassX  = groupX + kPrimary + minGapToBypass;
    const int outTrimX = bypassX + bypassW + minGapToBypass;

    const int bypassY = z4Y + ((z4H - bypassH) / 2);

    inTrim.setBounds  (inTrimX,  trimY, kPrimary, kPrimary);
    outTrim.setBounds (outTrimX, trimY, kPrimary, kPrimary);
    globalBypass.setBounds (bypassX, bypassY, bypassW, bypassH);

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
