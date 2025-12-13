#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Core/ParameterState.h"
#include "Core/Router.h"
#include "Core/MeterBus.h"
#include "Core/OverSamplingManager.h"

namespace compass
{
    CompassEQAudioProcessor::CompassEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
    {
        parameterState = std::make_unique<ParameterState>(*this);

        // Cache raw parameter pointers (Phase 2A wiring proof)
        auto &apvts = parameterState->getAPVTS();
        inTrim = apvts.getRawParameterValue(ParameterState::kInTrim);
        outTrim = apvts.getRawParameterValue(ParameterState::kOutTrim);
        hpfFreq = apvts.getRawParameterValue(ParameterState::kHPF);
        lpfFreq = apvts.getRawParameterValue(ParameterState::kLPF);

        router = std::make_unique<Router>();
        router->setParameterPointers(inTrim, outTrim, hpfFreq, lpfFreq);

        meterBus = std::make_unique<MeterBus>();
        oversamplingManager = std::make_unique<OversamplingManager>();
    }

    CompassEQAudioProcessor::~CompassEQAudioProcessor() = default;

    const juce::String CompassEQAudioProcessor::getName() const { return JucePlugin_Name; }

    bool CompassEQAudioProcessor::acceptsMidi() const { return false; }
    bool CompassEQAudioProcessor::producesMidi() const { return false; }
    bool CompassEQAudioProcessor::isMidiEffect() const { return false; }
    double CompassEQAudioProcessor::getTailLengthSeconds() const { return 0.0; }

    int CompassEQAudioProcessor::getNumPrograms() { return 1; }
    int CompassEQAudioProcessor::getCurrentProgram() { return 0; }
    void CompassEQAudioProcessor::setCurrentProgram(int) {}
    const juce::String CompassEQAudioProcessor::getProgramName(int) { return {}; }
    void CompassEQAudioProcessor::changeProgramName(int, const juce::String &) {}

    void CompassEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

        if (oversamplingManager)
            oversamplingManager->prepare(spec);
        if (router)
            router->prepare(spec);
    }

    void CompassEQAudioProcessor::releaseResources()
    {
        if (oversamplingManager)
            oversamplingManager->reset();
    }

#if !JucePlugin_PreferredChannelConfigurations
    bool CompassEQAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
    {
        const auto mainOut = layouts.getMainOutputChannelSet();
        if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
            return false;

        if (mainOut != layouts.getMainInputChannelSet())
            return false;

        return true;
    }
#endif

    void CompassEQAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
    {
        juce::ScopedNoDenormals noDenormals;
        juce::ignoreUnused(midi);

        // Phase 2A wiring proof (NO DSP yet)
        juce::ignoreUnused(inTrim, outTrim, hpfFreq, lpfFreq);

        // Phase 1 requirement: audio passthrough. Do nothing to buffer.
        // No gain, no clears, no allocations.

        // Still exercise architecture: call router (which will do nothing in Phase 1 stubs).
        if (router)
            router->process(buffer);

        if (meterBus)
            meterBus->pushBlock(buffer);
    }

    bool CompassEQAudioProcessor::hasEditor() const { return true; }

    juce::AudioProcessorEditor *CompassEQAudioProcessor::createEditor()
    {
        return new CompassEQAudioProcessorEditor(*this);
    }

    void CompassEQAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
    {
        if (parameterState)
        {
            auto state = parameterState->getAPVTS().copyState();
            std::unique_ptr<juce::XmlElement> xml(state.createXml());
            copyXmlToBinary(*xml, destData);
            return;
        }

        destData.reset();
    }

    void CompassEQAudioProcessor::setStateInformation(const void *data, int sizeInBytes)
    {
        if (!parameterState)
            return;

        std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
        if (xmlState != nullptr)
        {
            if (xmlState->hasTagName(parameterState->getAPVTS().state.getType()))
            {
                parameterState->getAPVTS().replaceState(juce::ValueTree::fromXml(*xmlState));
            }
        }
    }

} // namespace compass

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new compass::CompassEQAudioProcessor();
}