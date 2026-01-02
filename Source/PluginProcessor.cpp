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
    // Phase 4 Step 3 scaffold: allocate oversampling outside DSPCore::prepare/process
    dspCore.initOversampling (getTotalNumInputChannels());
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

    // HARD GLOBAL BYPASS (engine OFF): pass-through only, no trims, no DSP.
    // IMPORTANT: Pure Mode is NOT hard bypass; it continues through the normal processing path.
    const bool pureModeOn = getPureMode();

    // Phase 3: sync DSPCore pure-mode state once per block
    dspCore.setPureMode (pureModeOn);

    // Hard bypass = DSP/EQ OFF, but we still run trim-only gain staging + meters.
    const bool hardBypass = (bypassed && ! pureModeOn);

    // Input meter (post input-trim, pre-DSP): updates even when bypassed.
    const auto* inTrimParamMeter = apvts.getRawParameterValue (INPUT_TRIM_ID);
    float inPeak = 0.0f;
    for (int ch = 0; ch < getTotalNumInputChannels(); ++ch)
        inPeak = juce::jmax (inPeak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    const float inTrimDb   = inTrimParamMeter ? inTrimParamMeter->load() : 0.0f;
    const float inTrimGain = juce::Decibels::decibelsToGain (inTrimDb);

    const float in01 = juce::jlimit (0.0f, 1.0f, inPeak * inTrimGain);
    inMeter01.store (in01, std::memory_order_relaxed);
    // If bypassed, keep gain staging active (Input/Output Trim) while skipping DSP/EQ.
    const auto* outTrimParamMeter = apvts.getRawParameterValue (OUTPUT_TRIM_ID);
    const float outTrimDb   = outTrimParamMeter ? outTrimParamMeter->load() : 0.0f;
    const float outTrimGain = juce::Decibels::decibelsToGain (outTrimDb);

    if (hardBypass)
    {
        // Clear any output channels that don't have corresponding input channels
        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear (ch, 0, buffer.getNumSamples());

        // Pass-through for channels we do have
        const int n = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());
        for (int ch = 0; ch < n; ++ch)
        {
            // Copy in-place is safe even if host provides same buffer for in/out
            auto* dst = buffer.getWritePointer (ch);
            const auto* srcp = buffer.getReadPointer (ch);
            if (dst != srcp)
                std::memcpy (dst, srcp, (size_t) buffer.getNumSamples() * sizeof(float));
        }

        // Trim-only gain staging (Input Trim then Output Trim). Linear gains can be combined.
        buffer.applyGain (inTrimGain * outTrimGain);
    }
    // Engine path (normal OR Pure Mode): always feed targets + run dspCore.
    // Hard bypass: trim-only gain staging already applied above; skip DSP/EQ.
    if (! hardBypass)
    {
        const auto* inTrimParam  = apvts.getRawParameterValue (INPUT_TRIM_ID);
        const auto* outTrimParam = apvts.getRawParameterValue (OUTPUT_TRIM_ID);

        const auto* hpfParam     = apvts.getRawParameterValue (HPF_FREQUENCY_ID);
        const auto* lpfParam     = apvts.getRawParameterValue (LPF_FREQUENCY_ID);

        // EQ band params (Phase 2B wiring)
        const auto* lfFreqParam  = apvts.getRawParameterValue (LF_FREQUENCY_ID);
        const auto* lfGainParam  = apvts.getRawParameterValue (LF_GAIN_ID);

        const auto* lmfFreqParam = apvts.getRawParameterValue (LMF_FREQUENCY_ID);
        const auto* lmfGainParam = apvts.getRawParameterValue (LMF_GAIN_ID);
        const auto* lmfQParam    = apvts.getRawParameterValue (LMF_Q_ID);

        const auto* hmfFreqParam = apvts.getRawParameterValue (HMF_FREQUENCY_ID);
        const auto* hmfGainParam = apvts.getRawParameterValue (HMF_GAIN_ID);
        const auto* hmfQParam    = apvts.getRawParameterValue (HMF_Q_ID);

        const auto* hfFreqParam  = apvts.getRawParameterValue (HF_FREQUENCY_ID);
        const auto* hfGainParam  = apvts.getRawParameterValue (HF_GAIN_ID);

        #if JUCE_DEBUG
        {
            static bool sLastPure = false;
            static bool sLastByp  = false;
            const bool pureNow = getPureMode();
            if (pureNow != sLastPure || bypassed != sLastByp)
            {
                DBG(juce::String("[DSP] bypass=") + (bypassed ? "1" : "0")
                    + " pure=" + (pureNow ? "1" : "0"));
                sLastPure = pureNow;
                sLastByp  = bypassed;
            }
        }
        #endif
        dspCore.setTargets (
            inTrimParam  ? inTrimParam->load()  : 0.0f,
            outTrimParam ? outTrimParam->load() : 0.0f,
            hpfParam     ? hpfParam->load()     : Ranges::HPF_DEF,
            lpfParam     ? lpfParam->load()     : Ranges::LPF_DEF);

        dspCore.setBandTargets (
            lfFreqParam  ? lfFreqParam->load()  : Ranges::LF_FREQ_DEF,
            lfGainParam  ? lfGainParam->load()  : Ranges::GAIN_DEF,

            lmfFreqParam ? lmfFreqParam->load() : Ranges::LMF_FREQ_DEF,
            lmfGainParam ? lmfGainParam->load() : Ranges::GAIN_DEF,
            lmfQParam    ? lmfQParam->load()    : Ranges::Q_DEF,

            hmfFreqParam ? hmfFreqParam->load() : Ranges::HMF_FREQ_DEF,
            hmfGainParam ? hmfGainParam->load() : Ranges::GAIN_DEF,
            hmfQParam    ? hmfQParam->load()    : Ranges::Q_DEF,

            hfFreqParam  ? hfFreqParam->load()  : Ranges::HF_FREQ_DEF,
            hfGainParam  ? hfGainParam->load()  : Ranges::GAIN_DEF
        );

        dspCore.process (buffer);
    }

    // Output meter (post-DSP): measure the buffer after processing/bypass.

    float outPeak = 0.0f;
    for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch)
        outPeak = juce::jmax (outPeak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    const float out01 = juce::jlimit (0.0f, 1.0f, outPeak);
    outMeter01.store (out01, std::memory_order_relaxed);
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
