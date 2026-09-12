cmake_minimum_required(VERSION 3.22)

project(BluePrinter VERSION 1.1.0)

# CMake 4.x normalizes install() DESTINATION paths; with this policy set,
# backslashes in -DBLUEPRINTER_VST3_DIR="C:\Users\...\VST3" are treated as
# literal path separators instead of invalid escapes ("\U" errors).
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

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
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Source/icon.svg")

juce_generate_juce_header(BluePrinter)

target_sources(BluePrinter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WebViewEditor.cpp
    Source/SnippetLibrary.cpp
    Source/PluginChain.cpp
    Source/Vst3Library.cpp
    Source/KeyDetector.cpp
    Source/LooperGridMath.cpp
    Source/ChainStateMigration.cpp
    Source/ClickSynth.cpp
    Source/MetronomePlayer.cpp
    Source/MidiClockOutput.cpp)

target_include_directories(BluePrinter PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(BluePrinter PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    JUCE_ENABLE_DBG_OUTPUT=1)

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

# Emit PDBs in Release builds too. The crash handler writes backtraces as
# module+offset (see setCrashOp / bluePrinterCrashHandler); without a PDB
# only dumpbin could chase those offsets. With /Zi + /DEBUG the offset can
# be mapped straight back to a function (scripts/symbolize-crash.ps1 or the
# .map below).
if(MSVC)
    foreach(tgt BluePrinter BluePrinter_Standalone BluePrinter_VST3)
        if(TARGET ${tgt})
            target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:/Zi>)
            target_link_options(${tgt} PRIVATE $<$<CONFIG:Release>:/DEBUG>)
        endif()
    endforeach()

    # A linker /MAP file as well: plain-text symbol table (every public
    # symbol + its RVA), greppable without WinDbg/DIA. `dumpbin /headers`
    # gives the image base; a crash-info.txt "module + 0x..." offset maps
    # to the nearest function by scanning the map's executable-section
    # entries. The map lands next to the exe as BluePrinter.map.
    target_link_options(BluePrinter_Standalone PRIVATE
        $<$<CONFIG:Release>:/MAP:${CMAKE_CURRENT_BINARY_DIR}/BluePrinter_artefacts/Release/Standalone/BluePrinter.map>)
endif()

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

    # Embed the WebUI into the VST3 bundle at build time
    # (Contents/Resources/WebUI/dist — copy_directory copies the *contents*
    # of the source dir, so the destination must include the dist level) so
    # the editor can always find it, regardless of which process hosts the
    # plugin. A build-time copy (not an install rule) so any install that
    # copies the bundle straight from the build tree carries the UI too.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist/index.html")
        add_custom_command(TARGET BluePrinter_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/dist"
                "$<TARGET_FILE_DIR:BluePrinter_VST3>/../../Contents/Resources/WebUI/dist"
            COMMENT "Embedding WebUI into VST3 bundle")
    endif()
endif()

# ==============================================================================
# Tests
#
# Pure-logic unit tests: no audio device, no plugin instantiation, no
# WebView2. Configure with -DBUILD_TESTING=ON (the default), then:
#
#   cmake --build build --target BluePrinterTests
#   ctest --test-dir build --output-on-failure
#
# CI configures with BUILD_TESTING=ON (see .github/workflows/build.yml).
# ==============================================================================
include(CTest)

if(BUILD_TESTING)
    juce_add_console_app(BluePrinterTests PRODUCT_NAME "BluePrinter Tests")

    target_sources(BluePrinterTests PRIVATE
        Tests/TestRunner.cpp
        Tests/test_KeyDetector.cpp
        Tests/test_SnippetLibrary.cpp
        Tests/test_LooperGridMath.cpp
        Tests/test_ChainStateMigration.cpp
        Tests/test_MidiChannelFilter.cpp
        Tests/test_ClickSynth.cpp
        Tests/test_MetronomePlayer.cpp
        Tests/test_MidiClockMath.cpp
        Tests/test_MeterMath.cpp
        Tests/test_Meter.cpp
        Tests/test_LoopPlayback.cpp
        Tests/test_ChainRouting.cpp
        Tests/test_CaptureWrite.cpp
        # Production sources under test. Kept deliberately minimal so the
        # test target never pulls in WebView2, the plugin host, or the
        # audio device layer.
        Source/KeyDetector.cpp
        Source/SnippetLibrary.cpp
        Source/LooperGridMath.cpp
        Source/ChainStateMigration.cpp
        Source/ClickSynth.cpp
        Source/MetronomePlayer.cpp)

    juce_generate_juce_header(BluePrinterTests)

    target_include_directories(BluePrinterTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)

    target_link_libraries(BluePrinterTests PRIVATE
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_core)

    add_test(NAME BluePrinterTests COMMAND BluePrinterTests)

    # Bridge event-name parity (ticket 0013): kept as a Node script so it
    # can import bridge.js and compare its maps against WebViewEditor.h.
    # Requires node, which CI already has (setup-node before configure).
    find_program(BLUEPRINTER_NODE_EXECUTABLE NAMES node)

    if(BLUEPRINTER_NODE_EXECUTABLE)
        add_test(NAME BridgeEventParity
                 COMMAND ${BLUEPRINTER_NODE_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/Tests/check-bridge-events.mjs)
    else()
        message(STATUS "node not found: BridgeEventParity test disabled")
    endif()
endif()

