cmake_minimum_required(VERSION 3.22)
project(MixChecker VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Lokalna kopia JUCE
add_subdirectory(JUCE)

juce_add_plugin(MixChecker
    COMPANY_NAME "Alphastudio"
    PLUGIN_MANUFACTURER_CODE Alph
    PLUGIN_CODE Mxck
    FORMATS VST3
    PRODUCT_NAME "Mix Checker"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Analyzer Tools
)

target_sources(MixChecker PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# Grafika tla (Assets/background.png -> BinaryData)
juce_add_binary_data(MixCheckerAssets SOURCES Assets/background.png)

target_compile_definitions(MixChecker PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MixChecker
    PRIVATE
        MixCheckerAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)
