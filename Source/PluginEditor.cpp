#include "PluginEditor.h"
#include "PluginProcessor.h" // Best-practice: include processor here (not in the header)
#include "Phase1Spec.h"

using namespace phase1;

static void initKnob(juce::Slider &s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); // Phase 1 UI Addendum: NO value readouts
    s.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    s.setDoubleClickReturnValue(false, 0.0); // No modifier behaviors in Phase 1
}

CompassEQAudioProcessorEditor::CompassEQAudioProcessorEditor(CompassEQAudioProcessor &p)
    : AudioProcessorEditor(&p), proc(p), apvts(p.getAPVTS())
{
    initKnob(lfFreq);
    initKnob(lfGain);
    initKnob(lmfFreq);
    initKnob(lmfGain);
    initKnob(lmfQ);
    initKnob(hmfFreq);
    initKnob(hmfGain);
    initKnob(hmfQ);
    initKnob(hfFreq);
    initKnob(hfGain);

    initKnob(hpfFreq);
    initKnob(lpfFreq);
    initKnob(inTrim);
    initKnob(outTrim);

    bypass.setButtonText({}); // Phase 1 UI Addendum: no header/title/branding text
    bypass.setClickingTogglesState(true);

    addAndMakeVisible(lfFreq);
    addAndMakeVisible(lfGain);
    addAndMakeVisible(lmfFreq);
    addAndMakeVisible(lmfGain);
    addAndMakeVisible(lmfQ);
    addAndMakeVisible(hmfFreq);
    addAndMakeVisible(hmfGain);
    addAndMakeVisible(hmfQ);
    addAndMakeVisible(hfFreq);
    addAndMakeVisible(hfGain);

    addAndMakeVisible(hpfFreq);
    addAndMakeVisible(lpfFreq);
    addAndMakeVisible(inTrim);
    addAndMakeVisible(outTrim);

    addAndMakeVisible(bypass);

    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);

    // Attachments (canonical IDs)
    aLfFreq = std::make_unique<SliderAttachment>(apvts, LF_FREQUENCY_ID, lfFreq);
    aLfGain = std::make_unique<SliderAttachment>(apvts, LF_GAIN_ID, lfGain);

    aLmfFreq = std::make_unique<SliderAttachment>(apvts, LMF_FREQUENCY_ID, lmfFreq);
    aLmfGain = std::make_unique<SliderAttachment>(apvts, LMF_GAIN_ID, lmfGain);
    aLmfQ = std::make_unique<SliderAttachment>(apvts, LMF_Q_ID, lmfQ);

    aHmfFreq = std::make_unique<SliderAttachment>(apvts, HMF_FREQUENCY_ID, hmfFreq);
    aHmfGain = std::make_unique<SliderAttachment>(apvts, HMF_GAIN_ID, hmfGain);
    aHmfQ = std::make_unique<SliderAttachment>(apvts, HMF_Q_ID, hmfQ);

    aHfFreq = std::make_unique<SliderAttachment>(apvts, HF_FREQUENCY_ID, hfFreq);
    aHfGain = std::make_unique<SliderAttachment>(apvts, HF_GAIN_ID, hfGain);

    aHpfFreq = std::make_unique<SliderAttachment>(apvts, HPF_FREQUENCY_ID, hpfFreq);
    aLpfFreq = std::make_unique<SliderAttachment>(apvts, LPF_FREQUENCY_ID, lpfFreq);

    aInTrim = std::make_unique<SliderAttachment>(apvts, INPUT_TRIM_ID, inTrim);
    aOutTrim = std::make_unique<SliderAttachment>(apvts, OUTPUT_TRIM_ID, outTrim);

    aBypass = std::make_unique<ButtonAttachment>(apvts, GLOBAL_BYPASS_ID, bypass);

    setSize(980, 420);
    startTimerHz(30);
}

void CompassEQAudioProcessorEditor::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::grey.withAlpha(0.25f));
    g.drawRect(getLocalBounds(), 1);
}

void CompassEQAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12); // non-const

    // Right column for meters
    auto content = bounds;
    auto meterCol = content.removeFromRight(32);

    auto inArea = meterCol.removeFromTop(content.getHeight() / 2);
    inMeter.setBounds(inArea.reduced(4));
    outMeter.setBounds(meterCol.reduced(4));

    // Bottom strip for bypass button (small square, no text)
    auto bottomStrip = bounds.removeFromBottom(36);
    auto bypassArea = bottomStrip.removeFromRight(36);
    bypass.setBounds(bypassArea.reduced(6));

    // Grid area for knobs (15 required controls only)
    auto gridArea = content.withTrimmedBottom(40);

    const int cols = 5;
    const int rows = 3;

    const int cellW = gridArea.getWidth() / cols;
    const int cellH = gridArea.getHeight() / rows;

    auto cell = [&](int c, int r)
    {
        return juce::Rectangle<int>(
                   gridArea.getX() + c * cellW,
                   gridArea.getY() + r * cellH,
                   cellW,
                   cellH)
            .reduced(10);
    };

    lfFreq.setBounds(cell(0, 0));
    lfGain.setBounds(cell(1, 0));
    lmfFreq.setBounds(cell(2, 0));
    lmfGain.setBounds(cell(3, 0));
    lmfQ.setBounds(cell(4, 0));

    hmfFreq.setBounds(cell(0, 1));
    hmfGain.setBounds(cell(1, 1));
    hmfQ.setBounds(cell(2, 1));
    hfFreq.setBounds(cell(3, 1));
    hfGain.setBounds(cell(4, 1));

    hpfFreq.setBounds(cell(0, 2));
    lpfFreq.setBounds(cell(1, 2));
    inTrim.setBounds(cell(2, 2));
    outTrim.setBounds(cell(3, 2));
    // cell(4,2) intentionally unused (no extra UI permitted)
}

void CompassEQAudioProcessorEditor::timerCallback()
{
    inMeter.setLevel01(proc.getInputMeter01());
    outMeter.setLevel01(proc.getOutputMeter01());
}
