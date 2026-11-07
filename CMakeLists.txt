cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime: the .vst3 works in FL Studio without needing a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(SpectraDrive VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SPECTRADRIVE_BUILD_TESTS "Build the DSP engine test (no JUCE needed)" OFF)
option(SPECTRADRIVE_PLUGIN "Build the plugin (downloads JUCE)" ON)

if(SPECTRADRIVE_BUILD_TESTS)
    add_executable(engine_test tests/engine_test.cpp)
    target_include_directories(engine_test PRIVATE Source)
endif()

if(NOT SPECTRADRIVE_PLUGIN)
    return()
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON
    # Software renderer under Wine/Proton, see cmake/PatchJuceWine.cmake
    PATCH_COMMAND  ${CMAKE_COMMAND} -DJUCE_DIR=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchJuceWine.cmake)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SpectraDrive
    PRODUCT_NAME                "SpectraDrive"
    COMPANY_NAME                "n3tv01d"
    BUNDLE_ID                   "com.n3tv01d.spectradrive"
    PLUGIN_MANUFACTURER_CODE    N3tv
    PLUGIN_CODE                 Spdr
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Distortion)

# Fonts embedded in the plugin, so text never depends on what's installed. Under Wine JUCE
# asks for "Bitstream Vera Sans", which most prefixes don't have; text then fails to render.
juce_add_binary_data(SpectraDriveFonts
    SOURCES
        Resources/Fonts/DejaVuSans.ttf
        Resources/Fonts/DejaVuSansBold.ttf)
set_target_properties(SpectraDriveFonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(SpectraDrive PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SpectraDrive PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SpectraDrive
    PRIVATE
        SpectraDriveFonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
