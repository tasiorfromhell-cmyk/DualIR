cmake_minimum_required(VERSION 3.25)

project(DualIR VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "DualIR requires a Windows x64 toolchain. Use the windows-vs2022 preset.")
endif()

include(FetchContent)
# JUCE 8.0.12, pinned to an immutable upstream commit and archive checksum.
# The VST3 SDK is bundled with JUCE; no separate SDK download is required.
FetchContent_Declare(JUCE
    URL https://codeload.github.com/juce-framework/JUCE/tar.gz/29396c22c93392d6738e021b83196283d6e4d850
    URL_HASH SHA256=97cc01e0fe15278f6edadc895a3cf0aac2fd98bcd023823384d5ca7025beb91c
    TLS_VERIFY TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DualIR
    COMPANY_NAME "DualIR"
    BUNDLE_ID "com.dualir.plugin"
    PLUGIN_MANUFACTURER_CODE Dlir
    PLUGIN_CODE DuIR
    PRODUCT_NAME "DualIR"
    DESCRIPTION "Dual impulse response cabinet loader"
    FORMATS VST3
    VST3_CATEGORIES Fx Filter
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
)

juce_add_binary_data(DualIRGuiAssets
    HEADER_NAME DualIRGuiAssets.h
    NAMESPACE DualIRGuiAssets
    SOURCES logo.jpg
)

target_sources(DualIR PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DSP/CabProcessor.cpp
    Source/DSP/CabProcessor.h
    Source/DSP/DualCabEngine.cpp
    Source/DSP/DualCabEngine.h
    Source/IR/IRLoader.cpp
    Source/IR/IRLoader.h
    Source/GUI/MainPanel.cpp
    Source/GUI/MainPanel.h
)
target_compile_definitions(DualIR PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
if(MSVC)
    target_compile_options(DualIR PUBLIC /utf-8)
endif()
target_link_libraries(DualIR
    PRIVATE DualIRGuiAssets juce::juce_audio_processors juce::juce_gui_basics juce::juce_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
)

include(CTest)
if(BUILD_TESTING)
    add_executable(DualIRProcessorTests Tests/ProcessorSmokeTest.cpp)
    target_include_directories(DualIRProcessorTests PRIVATE Source "${juce_SOURCE_DIR}/modules")
    target_compile_definitions(DualIRProcessorTests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    target_link_libraries(DualIRProcessorTests PRIVATE DualIR)
    foreach(test_name IN ITEMS DSP IRLoader PluginIntegration)
        add_executable(DualIR${test_name}Tests Tests/${test_name}Tests.cpp)
        target_include_directories(DualIR${test_name}Tests PRIVATE Source "${juce_SOURCE_DIR}/modules")
        target_compile_definitions(DualIR${test_name}Tests PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
        target_link_libraries(DualIR${test_name}Tests PRIVATE DualIR)
        add_test(NAME DualIR.${test_name} COMMAND DualIR${test_name}Tests)
    endforeach()
    add_executable(DualIRVST3HostSmoke Tests/VST3HostSmoke.cpp)
    target_include_directories(DualIRVST3HostSmoke PRIVATE
        "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
    target_link_libraries(DualIRVST3HostSmoke PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(DualIRVST3HostSmoke DualIR_VST3)
    add_test(NAME DualIR.VST3Host COMMAND DualIRVST3HostSmoke $<TARGET_FILE:DualIR_VST3>)
    set_tests_properties(DualIR.DSP DualIR.IRLoader DualIR.PluginIntegration DualIR.VST3Host
        PROPERTIES TIMEOUT 60)
    add_test(NAME DualIR.ProcessorSmoke COMMAND DualIRProcessorTests)
    if(UNIX AND NOT APPLE)
        find_program(DUALIR_XVFB_RUN xvfb-run)
        if(DUALIR_XVFB_RUN)
            add_test(NAME DualIR.EditorSmoke
                COMMAND "${DUALIR_XVFB_RUN}" -a $<TARGET_FILE:DualIRProcessorTests> --editor)
        else()
            message(STATUS "xvfb-run not found: optional Linux editor-rendering test not registered")
        endif()
    else()
        add_test(NAME DualIR.EditorSmoke COMMAND DualIRProcessorTests --editor)
    endif()
endif()
