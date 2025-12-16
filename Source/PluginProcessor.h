#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "Core/DSPCore.h"

class CompassEQAudioProcessor final : public juce::AudioProcessor
{
public:
    CompassEQAudioProcessor();
    ~CompassEQAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

   #if !JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
   #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // UI-only meters (non-sonic). 0..1 scalar.
    float getInputMeter01() const noexcept  { return inMeter01.load(std::memory_order_relaxed); }
    float getOutputMeter01() const noexcept { return outMeter01.load(std::memory_order_relaxed); }

    // ===== Hidden Pure Mode flag (not a parameter) =====
    void setPureMode (bool enabled) noexcept { pureMode.store (enabled, std::memory_order_relaxed); }
    bool getPureMode() const noexcept        { return pureMode.load (std::memory_order_relaxed); }
    void togglePureMode() noexcept
    {
        setPureMode (! getPureMode());
    }

private:
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> inMeter01{0.0f};
    std::atomic<float> outMeter01{0.0f};

    std::atomic<bool> pureMode { false };
    
    compass::DSPCore dspCore;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompassEQAudioProcessor)
};
