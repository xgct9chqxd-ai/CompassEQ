#include "PluginEditor.h"
#include "Phase1Spec.h"

static constexpr int kEditorW = 760;
static constexpr int kEditorH = 420;

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
    // ----- Background -----
    g.fillAll (juce::Colours::black);

    // subtle weighting
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRect (getLocalBounds().removeFromTop (90));

    g.setColour (juce::Colours::white.withAlpha (0.02f));
    g.fillRect (getLocalBounds().removeFromBottom (70));

    // border
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRect (getLocalBounds(), 1);

    // ----- Fonts -----
    const auto titleFont  = juce::FontOptions (18.0f, juce::Font::bold);
    const auto headerFont = juce::FontOptions (11.0f, juce::Font::bold);
    const auto microFont  = juce::FontOptions (9.0f);

    // ----- Helpers -----
    auto drawHeaderAbove = [&g, &headerFont] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (juce::Colours::white.withAlpha (0.70f));
        g.setFont (headerFont);
        g.drawFittedText (txt, b.getX(), b.getY() + yOffset, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawLegendBelow = [&g, &microFont] (const char* txt, juce::Rectangle<int> b, int yOffset)
    {
        g.setColour (juce::Colours::white.withAlpha (0.40f));
        g.setFont (microFont);
        g.drawFittedText (txt, b.getX(), b.getBottom() + yOffset, b.getWidth(), 12, juce::Justification::centred, 1);
    };

    auto drawTick = [&g] (juce::Rectangle<int> b, int yOffset)
    {
        const int cx = b.getCentreX();
        const int y0 = b.getY() + yOffset;
        const int y1 = y0 + 6;
        g.setColour (juce::Colours::white.withAlpha (0.32f));
        g.drawLine ((float) cx, (float) y0, (float) cx, (float) y1, 1.0f);
    };

    auto drawColLabel = [&g, &headerFont] (const char* txt, juce::Rectangle<int> columnBounds, int y)
    {
        g.setColour (juce::Colours::white.withAlpha (0.78f));
        g.setFont (headerFont);
        g.drawFittedText (txt, columnBounds.getX(), y, columnBounds.getWidth(), 14, juce::Justification::centred, 1);
    };

    // ----- Title -----
    g.setColour (juce::Colours::white.withAlpha (0.88f));
    g.setFont (titleFont);
    g.drawText ("COMPASS EQ", 16, 10, 260, 24, juce::Justification::left);

    // ===== Phase 3.0 Add: column labels for stacks (paint-only) =====
    // Use existing knob bounds to define each column (no layout changes).
    const auto lfCol  = lfFreq.getBounds().getUnion (lfGain.getBounds());
    const auto lmfCol = lmfFreq.getBounds().getUnion (lmfQ.getBounds());
    const auto hmfCol = hmfFreq.getBounds().getUnion (hmfQ.getBounds());
    const auto hfCol  = hfFreq.getBounds().getUnion (hfGain.getBounds());

    // Place band labels slightly above the top knob row
    const int topY = juce::jmin (lfCol.getY(),
                                lmfCol.getY(),
                                hmfCol.getY(),
                                hfCol.getY());
    const int bandLabelY = topY - 18;

    drawColLabel ("LF",  lfCol,  bandLabelY);
    drawColLabel ("LMF", lmfCol, bandLabelY);
    drawColLabel ("HMF", hmfCol, bandLabelY);
    drawColLabel ("HF",  hfCol,  bandLabelY);

    // Filter header centered above each filter knob
    drawHeaderAbove ("HPF", hpfFreq.getBounds(), -14);
    drawHeaderAbove ("LPF", lpfFreq.getBounds(), -14);

    // Meter headers
    drawHeaderAbove ("IN",  inputMeter.getBounds(),  -14);
    drawHeaderAbove ("OUT", outputMeter.getBounds(), -14);

    // Trim headers
    drawHeaderAbove ("IN",  inTrim.getBounds(),  -14);
    drawHeaderAbove ("OUT", outTrim.getBounds(), -14);

    // ----- Micro legends under each knob -----
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

    // ----- Ticks above knobs -----
    drawTick (lfFreq.getBounds(),  -2);  drawTick (lfGain.getBounds(), -2);
    drawTick (lmfFreq.getBounds(), -2);  drawTick (lmfGain.getBounds(), -2); drawTick (lmfQ.getBounds(), -2);
    drawTick (hmfFreq.getBounds(), -2);  drawTick (hmfGain.getBounds(), -2); drawTick (hmfQ.getBounds(), -2);
    drawTick (hfFreq.getBounds(),  -2);  drawTick (hfGain.getBounds(), -2);

    drawTick (hpfFreq.getBounds(), -2);
    drawTick (lpfFreq.getBounds(), -2);

    drawTick (inTrim.getBounds(),  -2);
    drawTick (outTrim.getBounds(), -2);
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

    hpfFreq.setBounds (filtersStartX,                   filtersY, filterKnob, filterKnob);
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
}
