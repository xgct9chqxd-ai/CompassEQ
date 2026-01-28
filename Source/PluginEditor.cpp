#include "PluginEditor.h"
#include "Phase1Spec.h"
#include "UIStyle.h"
#include "BinaryData.h"
#include <cmath>

// Note: CompassLookAndFeel.h is included via PluginEditor.h

static constexpr int kEditorW = 900;
static constexpr int kEditorH = 500;
static constexpr int kPaintAuditOverlay = 0;

namespace
{
    // Helper to draw clean text labels (matched to Compressor style)
    static void drawLabelText(juce::Graphics& g, const char *txt,
                                     int x, int y, int w, int h,
                                     juce::Justification just,
                                     float alpha,
                                     juce::Colour col)
    {
        g.setColour(col.withAlpha(alpha));
        g.drawFittedText(txt, x, y, w, h, just, 1);
    }
}

// ===== Value popup helper =====
static inline juce::String popupTextFor(juce::Slider &s)
{
    const auto name = s.getName();
    const double value = s.getValue();
    if (name.containsIgnoreCase("frequency") || name.containsIgnoreCase("freq"))
    {
        constexpr double kOffEpsHz = 0.50;
        const bool isHPF = name.containsIgnoreCase("hpf");
        const bool isLPF = name.containsIgnoreCase("lpf");
        if (isHPF && (value <= (double) phase1::Ranges::HPF_DEF + kOffEpsHz)) return "OFF";
        if (isLPF && (value >= (double) phase1::Ranges::LPF_DEF - kOffEpsHz)) return "OFF";
        if (value >= 1000.0) return juce::String(value / 1000.0, 2) + " kHz";
        return juce::String(value, 2) + " Hz";
    }
    if (name.containsIgnoreCase("gain") || name.containsIgnoreCase("gr")) return juce::String(value, 1) + " dB";
    if (name.containsIgnoreCase("q")) return juce::String(value, 2);
    if (name.containsIgnoreCase("trim")) return juce::String(value, 1) + " dB";
    return s.getTextFromValue(value);
}

// ==============================================================================
// CONSTRUCTOR
// ==============================================================================
CompassEQAudioProcessorEditor::CompassEQAudioProcessorEditor(CompassEQAudioProcessor &p)
    : juce::AudioProcessorEditor(&p), proc(p), apvts(proc.getAPVTS()), valueReadout(*this),
      // Matched 3-argument constructor from header: (Processor, isInput, Editor)
      inputMeter(proc, true, *this), outputMeter(proc, false, *this),
      lookAndFeel(std::make_unique<CompassLookAndFeel>())
{
    setResizable(false, false);
    setSize(kEditorW, kEditorH);

    using namespace phase1;
    configureKnob(lfFreq, LF_FREQUENCY_ID, Ranges::LF_FREQ_DEF);
    configureKnob(lfGain, LF_GAIN_ID, Ranges::GAIN_DEF);
    configureKnob(lmfFreq, LMF_FREQUENCY_ID, Ranges::LMF_FREQ_DEF);
    configureKnob(lmfGain, LMF_GAIN_ID, Ranges::GAIN_DEF);
    configureKnob(lmfQ, LMF_Q_ID, Ranges::Q_DEF);
    configureKnob(hmfFreq, HMF_FREQUENCY_ID, Ranges::HMF_FREQ_DEF);
    configureKnob(hmfGain, HMF_GAIN_ID, Ranges::GAIN_DEF);
    configureKnob(hmfQ, HMF_Q_ID, Ranges::Q_DEF);
    configureKnob(hfFreq, HF_FREQUENCY_ID, Ranges::HF_FREQ_DEF);
    configureKnob(hfGain, HF_GAIN_ID, Ranges::GAIN_DEF);
    configureKnob(hpfFreq, HPF_FREQUENCY_ID, Ranges::HPF_DEF);
    configureKnob(lpfFreq, LPF_FREQUENCY_ID, Ranges::LPF_DEF);
    configureKnob(inTrim, INPUT_TRIM_ID, Ranges::TRIM_DEF);
    configureKnob(outTrim, OUTPUT_TRIM_ID, Ranges::TRIM_DEF);

    // Apply Color Coding - "Stealth Anodized" Palette (Deep/Dark to match Limiter vibe)
    // Blue -> Midnight Blue
    const auto colLF = juce::Colour(0xFF0F2436);
    lfFreq.setColour(juce::Slider::rotarySliderFillColourId, colLF);
    lfGain.setColour(juce::Slider::rotarySliderFillColourId, colLF);
    
    // Purple -> Midnight Violet
    const auto colLMF = juce::Colour(0xFF261A30);
    lmfFreq.setColour(juce::Slider::rotarySliderFillColourId, colLMF);
    lmfGain.setColour(juce::Slider::rotarySliderFillColourId, colLMF);
    lmfQ.setColour(juce::Slider::rotarySliderFillColourId, colLMF);

    // Green -> Midnight Moss
    const auto colHMF = juce::Colour(0xFF162B1C);
    hmfFreq.setColour(juce::Slider::rotarySliderFillColourId, colHMF);
    hmfGain.setColour(juce::Slider::rotarySliderFillColourId, colHMF);
    hmfQ.setColour(juce::Slider::rotarySliderFillColourId, colHMF);

    // Red -> Midnight Oxide
    const auto colHF = juce::Colour(0xFF331515);
    hfFreq.setColour(juce::Slider::rotarySliderFillColourId, colHF);
    hfGain.setColour(juce::Slider::rotarySliderFillColourId, colHF);

    // Filters match bands
    hpfFreq.setColour(juce::Slider::rotarySliderFillColourId, colLF);
    lpfFreq.setColour(juce::Slider::rotarySliderFillColourId, colHF);

    // Setup Names
    lfFreq.setName("LF Frequency"); lfGain.setName("LF Gain");
    lmfFreq.setName("LMF Frequency"); lmfGain.setName("LMF Gain"); lmfQ.setName("LMF Q");
    hmfFreq.setName("HMF Frequency"); hmfGain.setName("HMF Gain"); hmfQ.setName("HMF Q");
    hfFreq.setName("HF Frequency"); hfGain.setName("HF Gain");
    hpfFreq.setName("HPF Frequency"); lpfFreq.setName("LPF Frequency");
    inTrim.setName("Input Trim"); outTrim.setName("Output Trim");

    // Wire Readouts
    auto updateReadout = [this](CompassSlider &s) {
        if (activeSlider == &s) {
            valueReadout.setValueText(popupTextFor(s));
            valueReadout.show();
        }
    };
    auto wireReadout = [this, updateReadout](CompassSlider &s) {
        s.onDragStart = [this, updateReadout, &s] { activeSlider = &s; valueReadout.show(); updateReadout(s); };
        s.onValueChange = [updateReadout, &s] { if (s.isMouseButtonDown()) updateReadout(s); };
        s.onDragEnd = [this] { valueReadout.hide(); activeSlider = nullptr; };
    };

    auto addKnob = [this, wireReadout](CompassSlider &s) {
        wireReadout(s);
        addAndMakeVisible(s);
    };

    // ===== Active-band visual feedback flags (visual only) =====
    auto updateBandActiveFromGain = [&](CompassSlider& gain, std::initializer_list<CompassSlider*> affected)
    {
        // Compute a normalized amount (0..1) from the GAIN knob deviation, and apply it to the whole band group.
        const double def = gain.getDoubleClickReturnValue();
        const double v   = gain.getValue();
        const double dev = std::abs(v - def);

        const auto range = gain.getRange();
        const double start = range.getStart();
        const double end   = range.getEnd();
        const double maxDev = std::max(std::abs(def - start), std::abs(end - def));

        const float amt = (maxDev > 0.0) ? (float) juce::jlimit(0.0, 1.0, dev / maxDev) : 0.0f;

        for (auto* k : affected)
        {
            if (!k) continue;
            k->getProperties().set("bandAmt", amt);
            k->getProperties().set("bandActive", amt > 1.0e-6f);
            k->repaint();
        }
    };

    auto wrapOnValueChange = [](juce::Slider& s, std::function<void()> extra)
    {
        auto prev = s.onValueChange;
        s.onValueChange = [prev, extra]
        {
            if (prev) prev();
            if (extra) extra();
        };
    };

    addKnob(lfFreq); addKnob(lfGain);
    addKnob(lmfFreq); addKnob(lmfGain); addKnob(lmfQ);
    addKnob(hmfFreq); addKnob(hmfGain); addKnob(hmfQ);
    addKnob(hfFreq); addKnob(hfGain);
    addKnob(hpfFreq); addKnob(lpfFreq);
    addKnob(inTrim); addKnob(outTrim);

    // Initialize + wire active-band flags (gain != 0 dB)
    updateBandActiveFromGain(lfGain,  { &lfGain,  &lfFreq });
    updateBandActiveFromGain(lmfGain, { &lmfGain, &lmfFreq, &lmfQ });
    updateBandActiveFromGain(hmfGain, { &hmfGain, &hmfFreq, &hmfQ });
    updateBandActiveFromGain(hfGain,  { &hfGain,  &hfFreq });

    wrapOnValueChange(lfGain,  [&, this]{ updateBandActiveFromGain(lfGain,  { &lfGain,  &lfFreq }); });
    wrapOnValueChange(lmfGain, [&, this]{ updateBandActiveFromGain(lmfGain, { &lmfGain, &lmfFreq, &lmfQ }); });
    wrapOnValueChange(hmfGain, [&, this]{ updateBandActiveFromGain(hmfGain, { &hmfGain, &hmfFreq, &hmfQ }); });
    wrapOnValueChange(hfGain,  [&, this]{ updateBandActiveFromGain(hfGain,  { &hfGain,  &hfFreq }); });

    globalBypass.setButtonText("BYPASS");
    globalBypass.setClickingTogglesState(true);
    globalBypass.onAltClick = [this] { proc.togglePureMode(); globalBypass.repaint(); };
    addAndMakeVisible(globalBypass);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(valueReadout);
    valueReadout.toFront(false);

    // Attachments
    attLfFreq = std::make_unique<SliderAttachment>(apvts, phase1::LF_FREQUENCY_ID, lfFreq);
    attLfGain = std::make_unique<SliderAttachment>(apvts, phase1::LF_GAIN_ID, lfGain);
    attLmfFreq = std::make_unique<SliderAttachment>(apvts, phase1::LMF_FREQUENCY_ID, lmfFreq);
    attLmfGain = std::make_unique<SliderAttachment>(apvts, phase1::LMF_GAIN_ID, lmfGain);
    attLmfQ = std::make_unique<SliderAttachment>(apvts, phase1::LMF_Q_ID, lmfQ);
    attHmfFreq = std::make_unique<SliderAttachment>(apvts, phase1::HMF_FREQUENCY_ID, hmfFreq);
    attHmfGain = std::make_unique<SliderAttachment>(apvts, phase1::HMF_GAIN_ID, hmfGain);
    attHmfQ = std::make_unique<SliderAttachment>(apvts, phase1::HMF_Q_ID, hmfQ);
    attHfFreq = std::make_unique<SliderAttachment>(apvts, phase1::HF_FREQUENCY_ID, hfFreq);
    attHfGain = std::make_unique<SliderAttachment>(apvts, phase1::HF_GAIN_ID, hfGain);
    attHpfFreq = std::make_unique<SliderAttachment>(apvts, phase1::HPF_FREQUENCY_ID, hpfFreq);
    attLpfFreq = std::make_unique<SliderAttachment>(apvts, phase1::LPF_FREQUENCY_ID, lpfFreq);
    attInTrim = std::make_unique<SliderAttachment>(apvts, phase1::INPUT_TRIM_ID, inTrim);
    attOutTrim = std::make_unique<SliderAttachment>(apvts, phase1::OUTPUT_TRIM_ID, outTrim);
    attBypass = std::make_unique<ButtonAttachment>(apvts, phase1::GLOBAL_BYPASS_ID, globalBypass);

    // ===== Re-apply UI callbacks AFTER attachments =====
    // JUCE attachments may assign slider callbacks internally. Rebind our UI-only behavior here.
    auto updateReadout2 = [this](CompassSlider &s) {
        if (activeSlider == &s) {
            valueReadout.setValueText(popupTextFor(s));
            valueReadout.show();
        }
    };

    auto wireReadout2 = [this, updateReadout2](CompassSlider &s) {
        s.onDragStart = [this, updateReadout2, &s] { activeSlider = &s; valueReadout.show(); updateReadout2(s); };
        s.onValueChange = [updateReadout2, &s] { if (s.isMouseButtonDown()) updateReadout2(s); };
        s.onDragEnd = [this] { valueReadout.hide(); activeSlider = nullptr; };
    };

    auto updateBandActiveFromGain2 = [&](CompassSlider& gain, std::initializer_list<CompassSlider*> affected)
    {
        // Compute a normalized amount (0..1) from the GAIN knob deviation, and apply it to the whole band group.
        const double def = gain.getDoubleClickReturnValue();
        const double v   = gain.getValue();
        const double dev = std::abs(v - def);

        const auto range = gain.getRange();
        const double start = range.getStart();
        const double end   = range.getEnd();
        const double maxDev = std::max(std::abs(def - start), std::abs(end - def));

        const float amt = (maxDev > 0.0) ? (float) juce::jlimit(0.0, 1.0, dev / maxDev) : 0.0f;

        for (auto* k : affected)
        {
            if (!k) continue;
            k->getProperties().set("bandAmt", amt);
            k->getProperties().set("bandActive", amt > 1.0e-6f);
            k->repaint();
        }
    };

    auto wrapOnValueChange2 = [](juce::Slider& s, std::function<void()> extra)
    {
        auto prev = s.onValueChange;
        s.onValueChange = [prev, extra]
        {
            if (prev) prev();
            if (extra) extra();
        };
    };

    // Rebind readouts for all knobs
    wireReadout2(lfFreq); wireReadout2(lfGain);
    wireReadout2(lmfFreq); wireReadout2(lmfGain); wireReadout2(lmfQ);
    wireReadout2(hmfFreq); wireReadout2(hmfGain); wireReadout2(hmfQ);
    wireReadout2(hfFreq); wireReadout2(hfGain);
    wireReadout2(hpfFreq); wireReadout2(lpfFreq);
    wireReadout2(inTrim); wireReadout2(outTrim);

    // Initialize + wire active-band flags (gain != 0 dB)
    updateBandActiveFromGain2(lfGain,  { &lfGain,  &lfFreq });
    updateBandActiveFromGain2(lmfGain, { &lmfGain, &lmfFreq, &lmfQ });
    updateBandActiveFromGain2(hmfGain, { &hmfGain, &hmfFreq, &hmfQ });
    updateBandActiveFromGain2(hfGain,  { &hfGain,  &hfFreq });

    wrapOnValueChange2(lfGain,  [&, this]{ updateBandActiveFromGain2(lfGain,  { &lfGain,  &lfFreq }); });
    wrapOnValueChange2(lmfGain, [&, this]{ updateBandActiveFromGain2(lmfGain, { &lmfGain, &lmfFreq, &lmfQ }); });
    wrapOnValueChange2(hmfGain, [&, this]{ updateBandActiveFromGain2(hmfGain, { &hmfGain, &hmfFreq, &hmfQ }); });
    wrapOnValueChange2(hfGain,  [&, this]{ updateBandActiveFromGain2(hfGain,  { &hfGain,  &hfFreq }); });
}

CompassEQAudioProcessorEditor::~CompassEQAudioProcessorEditor()
{
    isTearingDown = true;
    cancelPendingUpdate();
    
    // Clear L&F refs
    lfFreq.setLookAndFeel(nullptr); lfGain.setLookAndFeel(nullptr);
    lmfFreq.setLookAndFeel(nullptr); lmfGain.setLookAndFeel(nullptr); lmfQ.setLookAndFeel(nullptr);
    hmfFreq.setLookAndFeel(nullptr); hmfGain.setLookAndFeel(nullptr); hmfQ.setLookAndFeel(nullptr);
    hfFreq.setLookAndFeel(nullptr); hfGain.setLookAndFeel(nullptr);
    hpfFreq.setLookAndFeel(nullptr); lpfFreq.setLookAndFeel(nullptr);
    inTrim.setLookAndFeel(nullptr); outTrim.setLookAndFeel(nullptr);
    
    lookAndFeel.reset();
}

void CompassEQAudioProcessorEditor::configureKnob(CompassSlider &s, const char *paramId, float defaultValue)
{
    (void) paramId;
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setRotaryParameters(juce::MathConstants<float>::pi * (210.0f / 180.0f), juce::MathConstants<float>::pi * (510.0f / 180.0f), true);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setPopupDisplayEnabled(false, false, this);
    s.setDoubleClickReturnValue(true, defaultValue);
    s.setScrollWheelEnabled(false);
    s.setVelocityModeParameters(0.4, 0, 0.0, true, juce::ModifierKeys::shiftModifier);
    s.setLookAndFeel(lookAndFeel.get());
}

// Background Drawing Logic
// Fixed unused parameter warnings
void CompassEQAudioProcessorEditor::renderStaticLayer(juce::Graphics &g, float, float)
{
    const auto editor = getLocalBounds();
    const int w = editor.getWidth();
    const int h = editor.getHeight();

    // 1. Base Background (Match Compressor: 0x0D0D0D)
    g.fillAll(juce::Colour(0xFF0D0D0D));

    // 2. Noise Texture (Match Compressor: Simple random speckle)
    {
        juce::Random rng(1234);
        for (int i = 0; i < 3000; ++i)
        {
            float x = rng.nextFloat() * w;
            float y = rng.nextFloat() * h;
            if (rng.nextBool()) g.setColour(juce::Colours::white.withAlpha(0.015f));
            else g.setColour(juce::Colours::black.withAlpha(0.04f));
            g.fillRect(x, y, 1.0f, 1.0f);
        }
    }

    // 3. Vignette (Match Compressor)
    {
        juce::ColourGradient vig(juce::Colours::transparentBlack, w/2.0f, h/2.0f,
                                 juce::Colours::black.withAlpha(0.6f), 0.0f, 0.0f, true);
        g.setGradientFill(vig);
        g.fillAll();
    }

    // 4. Industrial Screws (Match Compressor - with rings)
    auto drawScrew = [&](int cx, int cy) {
        float r = 6.0f;
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xFF151515), cx-r, cy-r, juce::Colour(0xFF2A2A2A), cx+r, cy+r, true));
        g.fillEllipse(cx-r, cy-r, r*2, r*2);
        
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawEllipse(cx-r, cy-r, r*2, r*2, 1.0f); // Added Ring
        
        juce::Path p; p.addStar({(float)cx,(float)cy}, 6, r*0.3f, r*0.6f);
        g.setColour(juce::Colour(0xFF050505)); g.fillPath(p);
    };
    drawScrew(14,14); drawScrew(w-14,14); drawScrew(14,h-14); drawScrew(w-14,h-14);

    // 5. Branding (Match Compressor Style)
    g.setFont(juce::FontOptions(15.0f));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText("COMPASS", 34, 18, 100, 20, juce::Justification::left);
    g.setColour(juce::Colour(0xFFE6A532)); // Gold
    g.drawText("// EQUALIZER", 105, 18, 120, 20, juce::Justification::left);

    // 6. Meter Wells (Match Limiter)
    auto drawMeterWell = [&](const juce::Rectangle<int> &bounds)
    {
        constexpr float kWellExpandPx        = 6.0f;
        constexpr float kWellCornerRadiusPx  = 4.0f;
        constexpr float kGlassAlpha          = 0.05f;

        const auto well = bounds.toFloat().expanded (kWellExpandPx);

        g.setColour (juce::Colour (0xFF0A0A0A));
        g.fillRoundedRectangle (well, kWellCornerRadiusPx);

        g.setColour (juce::Colours::white.withAlpha (kGlassAlpha));
        g.fillRoundedRectangle (well.reduced (1.0f), kWellCornerRadiusPx);
    };
    drawMeterWell(inputMeter.getBounds());
    drawMeterWell(outputMeter.getBounds());

    // 7. Connector Lines (UPDATED: More visible industrial vertical links)
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    auto drawConnector = [&](juce::Slider& top, juce::Slider& bot) {
        auto t = top.getBounds().getCentre();
        auto b = bot.getBounds().getCentre();
        g.drawLine((float)t.x, (float)t.y, (float)b.x, (float)b.y, 1.0f);
    };
    drawConnector(lfFreq, lfGain);
    drawConnector(lmfFreq, lmfQ); // Connects top to bottom of stack
    drawConnector(hmfFreq, hmfQ);
    drawConnector(hfFreq, hfGain);

    // 8. Labels & Markings
    // UPDATED: Brightness to match Limiter (0.9 for header, 0.65 for legend)
    const float kLabelAlpha = 0.90f;
    const float kLegendAlpha = 0.65f;

    auto drawLabel = [&](const char* txt, juce::Rectangle<int> b, int yOff, float alpha, juce::Colour c) {
        g.setFont(juce::FontOptions(11.0f));
        drawLabelText(g, txt, b.getX(), b.getY() + yOff, b.getWidth(), 14, juce::Justification::centred, alpha, c);
    };

    drawLabel("HPF", hpfFreq.getBounds(), -29, kLabelAlpha, juce::Colours::white);
    drawLabel("LPF", lpfFreq.getBounds(), -29, kLabelAlpha, juce::Colours::white);
    
    drawLabel("IN", inputMeter.getBounds(), inputMeter.getHeight() + 4, kLabelAlpha, juce::Colours::white);
    drawLabel("OUT", outputMeter.getBounds(), outputMeter.getHeight() + 4, kLabelAlpha, juce::Colours::white);

    // Band Headers
    drawLabel("LF", assetSlots.colLF, -20, kLabelAlpha, juce::Colours::white);
    drawLabel("LMF", assetSlots.colLMF, -20, kLabelAlpha, juce::Colours::white);
    drawLabel("HMF", assetSlots.colHMF, -20, kLabelAlpha, juce::Colours::white);
    drawLabel("HF", assetSlots.colHF, -20, kLabelAlpha, juce::Colours::white);

    // Legends (Use kLegendAlpha)
    auto drawLegend = [&](juce::Rectangle<int> b, const char* t) {
        drawLabel(t, b, b.getHeight() + 2, kLegendAlpha, juce::Colours::white);
    };
    drawLegend(lfFreq.getBounds(), "kHz"); drawLegend(lfGain.getBounds(), "dB");
    drawLegend(lmfFreq.getBounds(), "kHz"); drawLegend(lmfGain.getBounds(), "dB"); drawLegend(lmfQ.getBounds(), "Q");
    drawLegend(hmfFreq.getBounds(), "kHz"); drawLegend(hmfGain.getBounds(), "dB"); drawLegend(hmfQ.getBounds(), "Q");
    drawLegend(hfFreq.getBounds(), "kHz"); drawLegend(hfGain.getBounds(), "dB");
    
    drawLegend(inTrim.getBounds(), "dB");
    drawLegend(outTrim.getBounds(), "dB");
}

void CompassEQAudioProcessorEditor::ensureBackgroundNoiseTile() {}
void CompassEQAudioProcessorEditor::ensureCosmicHaze() {}

void CompassEQAudioProcessorEditor::handleAsyncUpdate() {
    staticCacheRebuildPending.store(false, std::memory_order_release);
    if (isTearingDown || !isVisible()) return;
    const float physicalScale = juce::jmax(1.0f, getPhysicalScaleLastPaint());
    const int pw = juce::roundToInt(getWidth() * physicalScale);
    const int ph = juce::roundToInt(getHeight() * physicalScale);
    if (pw <= 0 || ph <= 0) return;
    
    juce::Image img(juce::Image::ARGB, pw, ph, true);
    juce::Graphics cg(img);
    cg.addTransform(juce::AffineTransform::scale(physicalScale));
    renderStaticLayer(cg, getScaleKeyActive(), physicalScale);
    
    staticCache.image = std::move(img);
    staticCache.scaleKey = getScaleKeyActive();
    staticCache.pixelW = pw; staticCache.pixelH = ph;
    staticCacheDirty.store(false, std::memory_order_release);
    repaint();
}

void CompassEQAudioProcessorEditor::paint(juce::Graphics &g)
{
    const float physicalScale = (float)g.getInternalContext().getPhysicalPixelScaleFactor();
    physicalScaleLastPaint = physicalScale;
    
    // Scale logic
    const float rawKey = std::round(physicalScale * 100.0f) / 100.0f;
    float scaleKey = (std::abs(rawKey - 2.0f) <= 0.02f) ? 2.0f : ((std::abs(rawKey - 1.0f) <= 0.02f) ? 1.0f : rawKey);
    
    scaleKeyHistory[scaleKeyHistoryIndex] = scaleKey;
    scaleKeyHistoryIndex = (scaleKeyHistoryIndex + 1) % stabilityWindowSize;
    if (scaleKeyHistoryCount < stabilityWindowSize) scaleKeyHistoryCount++;

    bool isStable = (scaleKeyHistoryCount >= stabilityWindowSize);
    if (isStable)
    {
        const int mostRecentIdx = (scaleKeyHistoryIndex - 1 + stabilityWindowSize) % stabilityWindowSize;
        const float mostRecent = scaleKeyHistory[mostRecentIdx];
        for (int i = 0; i < stabilityWindowSize; ++i) {
            const int idx = (mostRecentIdx - i + stabilityWindowSize) % stabilityWindowSize;
            if (std::abs(scaleKeyHistory[idx] - mostRecent) > 0.001f) { isStable = false; break; }
        }
    }

    const auto currentTime = juce::Time::currentTimeMillis();
    
    // FIXED: Defined rateLimitMs here so it doesn't need to be in the header
    static constexpr juce::int64 rateLimitMs = 250;
    const bool rateLimitOk = (currentTime - lastScaleKeyChangeTime) >= rateLimitMs;

    if (isStable && rateLimitOk) {
        const int mostRecentIdx = (scaleKeyHistoryIndex - 1 + stabilityWindowSize) % stabilityWindowSize;
        const float candidateKey = scaleKeyHistory[mostRecentIdx];
        if (std::abs(candidateKey - scaleKeyActive) > 0.001f) {
            scaleKeyActive = candidateKey;
            lastScaleKeyChangeTime = currentTime;
            staticCacheDirty.store(true);
        }
    }

    const int pw = juce::roundToInt(getWidth() * physicalScale);
    const int ph = juce::roundToInt(getHeight() * physicalScale);
    bool cacheValid = staticCache.valid() && staticCache.pixelW == pw && staticCache.pixelH == ph && std::abs(staticCache.scaleKey - scaleKeyActive) < 0.001f;

    if (cacheValid) {
        g.drawImageTransformed(staticCache.image, juce::AffineTransform::scale(1.0f / physicalScale));
    } else {
        if (!staticCacheRebuildPending.exchange(true)) triggerAsyncUpdate();
        renderStaticLayer(g, scaleKeyActive, physicalScale); // Fallback
    }
}

void CompassEQAudioProcessorEditor::resized()
{
    // Updated: Centered Layout
    // Content width: 36+20+160+20+168+20+168+20+160+20+36 = 828px
    const int totalContentW = 828;
    const int startX = (getWidth() - totalContentW) / 2; // Center offset

    const int z1Y = 0, z1H = 64;
    const int z2Y = z1Y + z1H, z2H = 72;
    const int z3Y = z2Y + z2H, z3H = 240;
    const int z4Y = z3Y + z3H;

    int currentX = startX;

    // 1. Input Meter
    constexpr int meterW = 36; // Widened for Stereo
    const int meterBottomY = z4Y - 14;
    const int midY = z3Y;
    const int meterTopPad = 4;
    const int meterH = juce::jmax(220, meterBottomY - (midY + meterTopPad));
    inputMeter.setBounds(currentX, midY + meterTopPad, meterW, meterH);
    
    // Gap: Meter -> LF
    currentX += meterW + 20;

    // 2. Filters (Centered above bands)
    const int filterKnob = 58;
    const int filtersCenterY = z2Y - 10;
    const int centerEditorX = getWidth() / 2;
    hpfFreq.setBounds(centerEditorX - 160 - (filterKnob/2), filtersCenterY, filterKnob, filterKnob);
    lpfFreq.setBounds(centerEditorX + 160 - (filterKnob/2), filtersCenterY, filterKnob, filterKnob);

    // 3. Bands
    const int lfW = 160, lmfW = 168, hmfW = 168, hfW = 160;
    const int gap = 20;

    const int kPrimary = 72;
    const int kSecondary = 60;
    const int kTertiary = 48;
    
    const int stackSpacing = 16;
    const int stack3Top = (z3Y + 14) - 8;
    const int lmfFreqY = stack3Top;
    const int lmfQY = (z3Y + z3H - kTertiary - 10) - 8;
    const int lmfGainY = lmfFreqY + kSecondary + juce::jmax(0, (lmfQY - lmfFreqY - kSecondary - kPrimary) / 2);
    const int lfFreqY = z3Y + 50;
    const int lfGainY = lfFreqY + kSecondary + stackSpacing + 10;

    auto centerX = [](int colX, int colW, int knobW) { return colX + ((colW - knobW) / 2); };

    // LF
    int lfX = currentX;
    lfFreq.setBounds(centerX(lfX, lfW, kSecondary), lfFreqY, kSecondary, kSecondary);
    lfGain.setBounds(centerX(lfX, lfW, kPrimary), lfGainY, kPrimary, kPrimary);
    currentX += lfW + gap;

    // LMF
    int lmfX = currentX;
    lmfFreq.setBounds(centerX(lmfX, lmfW, kSecondary), lmfFreqY, kSecondary, kSecondary);
    lmfGain.setBounds(centerX(lmfX, lmfW, kPrimary), lmfGainY, kPrimary, kPrimary);
    lmfQ.setBounds(centerX(lmfX, lmfW, kTertiary), lmfQY, kTertiary, kTertiary);
    currentX += lmfW + gap;

    // HMF
    int hmfX = currentX;
    hmfFreq.setBounds(centerX(hmfX, hmfW, kSecondary), lmfFreqY, kSecondary, kSecondary);
    hmfGain.setBounds(centerX(hmfX, hmfW, kPrimary), lmfGainY, kPrimary, kPrimary);
    hmfQ.setBounds(centerX(hmfX, hmfW, kTertiary), lmfQY, kTertiary, kTertiary);
    currentX += hmfW + gap;

    // HF
    int hfX = currentX;
    hfFreq.setBounds(centerX(hfX, hfW, kSecondary), lfFreqY, kSecondary, kSecondary);
    hfGain.setBounds(centerX(hfX, hfW, kPrimary), lfGainY, kPrimary, kPrimary);
    currentX += hfW + gap;

    // Output Meter
    outputMeter.setBounds(currentX, midY + meterTopPad, meterW, meterH);

    // 4. Trims & Bypass
    auto zone4 = getLocalBounds().removeFromBottom(84).reduced(24, 0);
    const int bypassCy = zone4.getCentreY() - 10;
    const int trimCy = bypassCy + 4;
    const int trimSize = 58;
    
    globalBypass.setBounds(juce::Rectangle<int>(0, 0, 90, 24).withCentre({zone4.getCentreX(), bypassCy}));
    
    // Align trims with meters (centered under respective meter)
    inTrim.setBounds(juce::Rectangle<int>(0, 0, trimSize, trimSize).withCentre({inputMeter.getBounds().getCentreX(), trimCy}));
    outTrim.setBounds(juce::Rectangle<int>(0, 0, trimSize, trimSize).withCentre({outputMeter.getBounds().getCentreX(), trimCy}));

    // Update Asset Slots
    assetSlots.bandsZone = lfFreq.getBounds().getUnion(hfGain.getBounds()).getUnion(lmfQ.getBounds()).expanded(10);
    assetSlots.colLF = lfFreq.getBounds().getUnion(lfGain.getBounds());
    assetSlots.colLMF = lmfFreq.getBounds().getUnion(lmfQ.getBounds());
    assetSlots.colHMF = hmfFreq.getBounds().getUnion(hmfQ.getBounds());
    assetSlots.colHF = hfFreq.getBounds().getUnion(hfGain.getBounds());

    // Center Value Readout
    valueReadout.setBounds((getWidth() - 160) / 2, 48, 160, 20);
    staticCacheDirty.store(true);
}

void CompassEQAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // === Bypass Button Overlay (Matches Limiter Link Style) ===
    {
        const auto b = globalBypass.getBounds().toFloat();
        const bool isOn = globalBypass.getToggleState();
        const bool isDown = globalBypass.isMouseButtonDown();

        const auto rOuter = b.reduced (2.0f);
        if (!rOuter.isEmpty())
        {
            // Dark Base
            g.setColour (juce::Colours::silver.withAlpha (0.5f));
            g.drawRoundedRectangle (rOuter, 4.0f, 2.0f);
            
            // Border (Gold when ON, Grey when OFF)
            g.setColour(isOn ? juce::Colour(0xFFE6A532).withAlpha(0.5f) : juce::Colours::white.withAlpha(0.2f));
            g.drawRoundedRectangle (rOuter, 4.0f, 1.5f);

            // Subtle Tint Fill when ON
            if (isOn) {
                g.setColour(juce::Colour(0xFFE6A532).withAlpha(0.15f));
                g.fillRoundedRectangle(rOuter, 4.0f);
            }
            
            // Text (Gold when ON, White Ghost when OFF)
            // Fixed: Use safe FontOptions constructor
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            
            g.setColour(isOn ? juce::Colour(0xFFE6A532) : juce::Colours::white.withAlpha(0.5f));
            g.drawText("BYPASS", rOuter, juce::Justification::centred, false);
            
            // Pure Mode Small Indicator (Inside/Right of Button)
            if (proc.getPureMode())
            {
                // Small blue dot/pill to the right of the text
                // FIXED: Create a copy of rOuter before calling removeFromRight to respect const
                auto areaForPip = rOuter;
                auto pill = areaForPip.removeFromRight(14).reduced(3.0f);
                
                g.setColour(juce::Colour(0xFF1E90FF));
                g.fillEllipse(pill.getCentreX() - 2.0f, pill.getCentreY() - 2.0f, 4.0f, 4.0f);
            }
        }
    }
}
