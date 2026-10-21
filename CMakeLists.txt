cmake_minimum_required(VERSION 3.15)

# Plasma: a wavetable synth built on the GPLv3 Vital source, packaged as CLAP (+ VST3).
# Version tracks the Vital release the source corresponds to, so .vital presets load (newer versions are rejected).
project(Plasma VERSION 1.5.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(third_party/JUCE)
add_subdirectory(third_party/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(Plasma
  PRODUCT_NAME "Plasma"
  COMPANY_NAME "Plasma"
  COMPANY_WEBSITE "https://github.com/"
  BUNDLE_ID "net.etchebarne.plasma"
  PLUGIN_MANUFACTURER_CODE Plsm
  PLUGIN_CODE Plsm
  FORMATS VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth)

file(GLOB PLASMA_FONTS fonts/*)
file(GLOB PLASMA_ICONS icons/*.svg)
file(GLOB PLASMA_TUNINGS tunings/*.scl)
juce_add_binary_data(PlasmaBinaryData
  HEADER_NAME BinaryData.h
  NAMESPACE BinaryData
  SOURCES default.vitalskin ${PLASMA_FONTS} ${PLASMA_ICONS} ${PLASMA_TUNINGS})

target_sources(Plasma PRIVATE
  src/unity_build/common.cpp
  src/unity_build/synthesis.cpp
  src/unity_build/interface_editor_components.cpp
  src/unity_build/interface_editor_sections.cpp
  src/unity_build/interface_editor_sections2.cpp
  src/unity_build/interface_look_and_feel.cpp
  src/unity_build/interface_wavetable.cpp
  src/unity_build/plugin.cpp)

target_include_directories(Plasma PRIVATE
  src/juce_header
  src/common
  src/common/wavetable
  src/interface/editor_components
  src/interface/editor_sections
  src/interface/look_and_feel
  src/interface/wavetable
  src/interface/wavetable/editors
  src/interface/wavetable/overlays
  src/plugin
  src/synthesis/synth_engine
  src/synthesis/effects
  src/synthesis/filters
  src/synthesis/framework
  src/synthesis/lookups
  src/synthesis/modulators
  src/synthesis/modules
  src/synthesis/producers
  src/synthesis/utilities
  third_party)

string(TIMESTAMP PLASMA_BUILD_DATE "%Y %m %d %H %M")

target_compile_definitions(Plasma PUBLIC
  NO_AUTH=1
  BUILD_DATE=${PLASMA_BUILD_DATE}
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_ALSA=0
  JUCE_JACK=0
  JUCE_USE_XRANDR=0
  JUCE_OPENGL3=1
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_MODAL_LOOPS_PERMITTED=1)

target_compile_options(Plasma PRIVATE
  -ffast-math -ftree-vectorize -ftree-slp-vectorize -funroll-loops
  $<$<COMPILE_LANGUAGE:CXX>:-w>)

target_link_libraries(Plasma
  PRIVATE
    PlasmaBinaryData
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_opengl
  PUBLIC
    juce::juce_recommended_config_flags)

clap_juce_extensions_plugin(TARGET Plasma
  CLAP_ID "net.etchebarne.plasma"
  CLAP_FEATURES instrument synthesizer stereo)
