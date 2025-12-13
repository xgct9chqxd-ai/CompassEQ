cmake_minimum_required(VERSION 3.22)

project(CompassEQ VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Phase 1 guardrail: make JUCE path configurable (for Grok portability)
set(JUCE_DIR "" CACHE PATH "Path to JUCE folder (e.g. /Users/johncollins/JUCE)")

if (JUCE_DIR STREQUAL "")
  message(FATAL_ERROR "JUCE_DIR is not set. Set -DJUCE_DIR=/full/path/to/JUCE")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(CompassEQ
  COMPANY_NAME "Compass"
  PLUGIN_MANUFACTURER_CODE "JCOL"
  PLUGIN_CODE "CPEQ"
  FORMATS VST3
  PRODUCT_NAME "Compass EQ"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(CompassEQ PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp

  Source/Core/ParameterState.cpp
  Source/Core/DSPCore.cpp
  Source/Core/Router.cpp
  Source/Core/MeterBus.cpp
  Source/Core/OverSamplingManager.cpp
)

target_include_directories(CompassEQ PRIVATE
  Source
  Source/Core
)

# Phase 1 restriction: only what the template needs (no "just in case" modules)
target_link_libraries(CompassEQ PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
)

juce_generate_juce_header(CompassEQ)

target_compile_definitions(CompassEQ PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
)
