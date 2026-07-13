cmake_minimum_required(VERSION 3.25)
project(DivergePlugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.9
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DivergePlugin
  COMPANY_NAME "senhuang42"
  BUNDLE_ID "com.senhuang42.diverge"
  PLUGIN_MANUFACTURER_CODE Snhg
  PLUGIN_CODE Dvge
  FORMATS AU VST3 Standalone
  PRODUCT_NAME "Diverge"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(DivergePlugin PRIVATE
  Source/JobRunner.cpp
  Source/JobRunner.h
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
)

target_compile_definitions(DivergePlugin PUBLIC
  DIVERGE_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DivergePlugin PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
)
