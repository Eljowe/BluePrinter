cmake_minimum_required(VERSION 3.22)

project(Obstacle VERSION 1.0.0)

set(JUCE_DIR "C:/JUCE/JUCE" CACHE PATH "Path to the JUCE source tree")

add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(Obstacle
    COMPANY_NAME "Retrokielto"
    PRODUCT_NAME "Obstacle"
    BUNDLE_ID "com.retrokielto.Obstacle"
    PLUGIN_MANUFACTURER_CODE Rtrk
    PLUGIN_CODE Obst
    FORMATS VST3 Standalone
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

juce_generate_juce_header(Obstacle)

target_sources(Obstacle PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WebViewEditor.cpp)

target_include_directories(Obstacle PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(Obstacle PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_CURL=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

target_link_libraries(Obstacle PRIVATE
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
    juce::juce_gui_extra)