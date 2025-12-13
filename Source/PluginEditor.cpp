#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace compass
{
CompassEQAudioProcessorEditor::CompassEQAudioProcessorEditor (CompassEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (400, 200);
}

CompassEQAudioProcessorEditor::~CompassEQAudioProcessorEditor() = default;

void CompassEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void CompassEQAudioProcessorEditor::resized()
{
}
} // namespace compass
