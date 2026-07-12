cmake_minimum_required(VERSION 3.22)

project(BluePrinter VERSION 1.0.0)

set(JUCE_DIR "C:/JUCE/JUCE" CACHE PATH "Path to the JUCE source tree")

add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(BluePrinter
    COMPANY_NAME "Retrokielto"
    PRODUCT_NAME "BluePrinter"
    BUNDLE_ID "com.retrokielto.BluePrinter"
    PLUGIN_MANUFACTURER_CODE Rtrk
    PLUGIN_CODE BlPt
    FORMATS VST3 Standalone
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

juce_generate_juce_header(BluePrinter)

target_sources(BluePrinter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WebViewEditor.cpp
    Source/SnippetLibrary.cpp
    Source/PluginChain.cpp
    Source/KeyDetector.cpp)

target_include_directories(BluePrinter PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(BluePrinter PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

target_link_libraries(BluePrinter PRIVATE
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

# ==============================================================================
# Install rules
#
# After building, run from an *elevated* PowerShell so the install can write
# to Program Files:
#
#   cmake --build build --config Release --target INSTALL
#   # or equivalently:
#   cmake --install build --config Release
#
# By default the standalone goes to C:\Program Files\BluePrinter\BluePrinter.exe
# and the VST3 to C:\Program Files\Common Files\VST3\BluePrinter.vst3 (so any
# DAW that scans the standard VST3 folder picks it up). A Start Menu
# shortcut named "BluePrinter" is created in the *per-user* Start Menu
# folder so Windows search finds it — that one doesn't need admin.
#
# To install without admin, point CMAKE_INSTALL_PREFIX at a user folder:
#   cmake --install build --config Release --prefix C:\Users\<you>\BluePrinter
# Then add the VST3 location to your DAW's scan paths.
# ==============================================================================
if(WIN32)
    # Where the VST3 bundle ends up. DAWs scan this folder by default.
    # Override with -DBLUEPRINTER_VST3_DIR="..." if you keep your VST3s
    # somewhere non-standard.
    if(NOT DEFINED BLUEPRINTER_VST3_DIR)
        set(BLUEPRINTER_VST3_DIR "C:/Program Files/Common Files/VST3")
    endif()

    # Standalone .exe → <prefix>/BluePrinter.exe
    install(PROGRAMS $<TARGET_FILE:BluePrinter_Standalone>
        DESTINATION .
        RENAME BluePrinter.exe)

    # VST3 bundle → <BLUEPRINTER_VST3_DIR>/BluePrinter.vst3
    # Note: $<TARGET_FILE:BluePrinter_VST3> points at the inner .vst3
    # DLL inside the bundle, not at the bundle directory itself. JUCE
    # emits the bundle at <bin>/BluePrinter_artefacts/<Config>/VST3/,
    # so we go through $<CONFIG> to pick the right one at install time.
    install(DIRECTORY
        "${CMAKE_BINARY_DIR}/BluePrinter_artefacts/$<CONFIG>/VST3/BluePrinter.vst3"
        DESTINATION "${BLUEPRINTER_VST3_DIR}")

    # Start Menu shortcut so Windows search picks up "BluePrinter".
    # install(SCRIPT) runs the .cmake at install time on the host.
    install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/install-shortcut.cmake")

    # Ship the WebUI build output next to the standalone so the running
    # app can find WebUI/dist/index.html. findLocalWebUiDistIndex() in
    # WebViewEditor.cpp walks up from the executable looking for this
    # directory. Skip silently if it doesn't exist (e.g. a build done
    # without running `npm install && npm run build` first).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist/index.html")
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist"
            DESTINATION WebUI)
    endif()
endif()