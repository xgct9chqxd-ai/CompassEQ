#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Phase1Spec.h"

using namespace phase1;

CompassEQAudioProcessor::CompassEQAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout CompassEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // EQ Bands
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        LF_FREQUENCY_ID, "LF Frequency",
        makeHzRange (Ranges::LF_FREQ_MIN, Ranges::LF_FREQ_MAX),
        Ranges::LF_FREQ_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        LF_GAIN_ID, "LF Gain",
        makeDbRange (Ranges::GAIN_MIN, Ranges::GAIN_MAX),
        Ranges::GAIN_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        LMF_FREQUENCY_ID, "LMF Frequency",
        makeHzRange (Ranges::LMF_FREQ_MIN, Ranges::LMF_FREQ_MAX),
        Ranges::LMF_FREQ_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        LMF_GAIN_ID, "LMF Gain",
        makeDbRange (Ranges::GAIN_MIN, Ranges::GAIN_MAX),
        Ranges::GAIN_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        LMF_Q_ID, "LMF Q",
        makeQRange (Ranges::Q_MIN, Ranges::Q_MAX),
        Ranges::Q_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        HMF_FREQUENCY_ID, "HMF Frequency",
        makeHzRange (Ranges::HMF_FREQ_MIN, Ranges::HMF_FREQ_MAX),
        Ranges::HMF_FREQ_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        HMF_GAIN_ID, "HMF Gain",
        makeDbRange (Ranges::GAIN_MIN, Ranges::GAIN_MAX),
        Ranges::GAIN_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        HMF_Q_ID, "HMF Q",
        makeQRange (Ranges::Q_MIN, Ranges::Q_MAX),
        Ranges::Q_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        HF_FREQUENCY_ID, "HF Frequency",
        makeHzRange (Ranges::HF_FREQ_MIN, Ranges::HF_FREQ_MAX),
        Ranges::HF_FREQ_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        HF_GAIN_ID, "HF Gain",
        makeDbRange (Ranges::GAIN_MIN, Ranges::GAIN_MAX),
        Ranges::GAIN_DEF));

    // Filters
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        HPF_FREQUENCY_ID, "HPF Frequency",
        makeHzRange (Ranges::HPF_MIN, Ranges::HPF_MAX),
        Ranges::HPF_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        LPF_FREQUENCY_ID, "LPF Frequency",
        makeHzRange (Ranges::LPF_MIN, Ranges::LPF_MAX),
        Ranges::LPF_DEF));

    // Gain staging
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        INPUT_TRIM_ID, "Input Trim",
        makeDbRange (Ranges::TRIM_MIN, Ranges::TRIM_MAX),
        Ranges::TRIM_DEF));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        OUTPUT_TRIM_ID, "Output Trim",
        makeDbRange (Ranges::TRIM_MIN, Ranges::TRIM_MAX),
        Ranges::TRIM_DEF));

    // Transport
    layout.add (std::make_unique<juce::AudioParameterBool>(
        GLOBAL_BYPASS_ID, "Global Bypass", false));

    return layout;
}

void CompassEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dspCore.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());

    inMeter01.store (0.0f, std::memory_order_relaxed);
    outMeter01.store (0.0f, std::memory_order_relaxed);
}

#if !JucePlugin_PreferredChannelConfigurations
bool CompassEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainIn.isDisabled() || mainOut.isDisabled())
        return false;
    return (mainIn == mainOut);
}
#endif

void CompassEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const auto* bypassParam = apvts.getRawParameterValue (GLOBAL_BYPASS_ID);
    const bool bypassed = (bypassParam != nullptr && bypassParam->load() >= 0.5f);

    if (! bypassed)
    {
        const auto* inTrimParam  = apvts.getRawParameterValue (INPUT_TRIM_ID);
        const auto* outTrimParam = apvts.getRawParameterValue (OUTPUT_TRIM_ID);
        const auto* hpfParam     = apvts.getRawParameterValue (HPF_FREQUENCY_ID);
        const auto* lpfParam     = apvts.getRawParameterValue (LPF_FREQUENCY_ID);

        dspCore.setTargets (
            inTrimParam  ? inTrimParam->load()  : 0.0f,
            outTrimParam ? outTrimParam->load() : 0.0f,
            hpfParam     ? hpfParam->load()     : Ranges::HPF_DEF,
            lpfParam     ? lpfParam->load()     : Ranges::LPF_DEF);

        dspCore.process (buffer);
    }

    float peak = 0.0f;
    for (int ch = 0; ch < getTotalNumInputChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    const float p01 = juce::jlimit (0.0f, 1.0f, peak);
    inMeter01.store  (p01, std::memory_order_relaxed);
    outMeter01.store (p01, std::memory_order_relaxed);
}

void CompassEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void CompassEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* CompassEQAudioProcessor::createEditor()
{
    return new CompassEQAudioProcessorEditor (*this);
}

// IMPORTANT: must be global scope (NOT in a namespace)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompassEQAudioProcessor();
}
