cmake_minimum_required(VERSION 3.22)

project(CompassEQ VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE path provided via -DJUCE_DIR=/Users/johncollins/JUCE
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(CompassEQ
    COMPANY_NAME "Compass"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE

    PLUGIN_MANUFACTURER_CODE Comp
    PLUGIN_CODE CqE1

    FORMATS VST3
    PRODUCT_NAME "Compass EQ"
)

# REQUIRED for <JuceHeader.h>
juce_generate_juce_header(CompassEQ)

# Phase 1: compile ONLY Phase 1 files
target_sources(CompassEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Phase1Spec.h
)

target_compile_definitions(CompassEQ PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0

    # VST3-only build: prevent JUCE VST3<->VST2 replacement/automation guard from firing
    JUCE_VST3_CAN_REPLACE_VST2=0
    JucePlugin_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(CompassEQ PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)