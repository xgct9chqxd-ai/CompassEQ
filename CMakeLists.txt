cmake_minimum_required(VERSION 3.22)

project(CompassEQ VERSION 0.8.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE path provided via -DJUCE_DIR=/Users/johncollins/JUCE
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(CompassEQ
    VERSION 0.8.0
    PLUGIN_VERSION "0.8.0-dev"
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

# We are not shipping VST2, so do NOT claim VST3 replaces VST2
target_compile_definitions(CompassEQ PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)

# Some JUCE setups compile VST3 code in a generated target too
if (TARGET CompassEQ_VST3)
    target_compile_definitions(CompassEQ_VST3 PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)
endif()

# REQUIRED for <JuceHeader.h>
juce_generate_juce_header(CompassEQ)

# Add the binary data (grain + cosmic haze)
juce_add_binary_data(CompassResources
    SOURCES
        Resources/grain_texture.png
        Resources/cosmic_haze.png
)

# Phase 1: compile ONLY Phase 1 files
target_sources(CompassEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Phase1Spec.h
)

# ... your compile definitions ...

target_link_libraries(CompassEQ
    PRIVATE
        CompassResources                  # <-- This links your image
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)