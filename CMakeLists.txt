cmake_minimum_required(VERSION 3.22)

project(ORION_SPARK VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# DSP + processor sources shared by the plugin and the offline test runner.
set(ORION_CORE_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Pedal.cpp
    src/Presets.cpp
    src/Tuner.cpp
    src/ui/Render.cpp
    src/ui/Logo.cpp
    src/pedals/NoiseGate.cpp
    src/pedals/Overdrive.cpp
    src/pedals/BritStack.cpp
    src/pedals/FourTrackPre.cpp
    src/pedals/ModPedal.cpp
    src/pedals/TapeEcho.cpp
    src/pedals/SpringReverb.cpp)

juce_add_plugin(OrionSpark
    COMPANY_NAME "Jonathan Neroth"
    BUNDLE_ID "local.neroth.orionspark"
    PLUGIN_MANUFACTURER_CODE Nrth
    PLUGIN_CODE Orsk
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Orion Spark"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Orion Spark needs audio input to process your guitar."
    COPY_PLUGIN_AFTER_BUILD TRUE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion Delay Reverb)

target_sources(OrionSpark PRIVATE ${ORION_CORE_SOURCES})
target_include_directories(OrionSpark PRIVATE src)

target_compile_definitions(OrionSpark PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OrionSpark
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline test runner: pushes test signals through the real processor and
# checks for NaN/Inf, runaway feedback, decay and bypass transparency.
juce_add_console_app(OrionTests PRODUCT_NAME "OrionTests")
target_sources(OrionTests PRIVATE tests/OrionTests.cpp ${ORION_CORE_SOURCES})
target_include_directories(OrionTests PRIVATE src)
target_compile_definitions(OrionTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    ORION_TESTS=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(OrionTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
