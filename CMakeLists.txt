cmake_minimum_required(VERSION 3.22)

project(OilSlick VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local copy if you have one (-DOILSLICK_JUCE_PATH=...),
# otherwise download JUCE 8 automatically.
# ---------------------------------------------------------------------------
set(OILSLICK_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(OILSLICK_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(OILSLICK_BUILD_TESTS "Build the offline tests" ON)

# The DSP engine: plain C++17, no JUCE, builds anywhere (Windows x64, Linux ARM64, macOS).
add_library(oilslick_engine INTERFACE)
target_include_directories(oilslick_engine INTERFACE Source/engine)

if(OILSLICK_BUILD_TESTS)
    add_executable(oilslick_render_test Tests/RenderTest.cpp)
    target_link_libraries(oilslick_render_test PRIVATE oilslick_engine)
endif()

if(OILSLICK_BUILD_PLUGIN)
    if(OILSLICK_JUCE_PATH)
        add_subdirectory(${OILSLICK_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(OILSLICK_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND OILSLICK_FORMATS AU)
    endif()

    juce_add_plugin(OilSlick
        COMPANY_NAME "Kevin Kaye"
        COMPANY_WEBSITE ""
        BUNDLE_ID "uk.kevinkaye.oilslick"
        PLUGIN_MANUFACTURER_CODE Kkay
        PLUGIN_CODE Olsk
        FORMATS ${OILSLICK_FORMATS}
        PRODUCT_NAME "OilSlick"
        DESCRIPTION "Polyphonic pitch, slick and unstable"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Modulation Pitch Shift)

    juce_add_binary_data(OilSlickData SOURCES Resources/RobotoCondensed-Bold.ttf)

    set(OILSLICK_SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PedalControl.cpp
        Source/Keeper.cpp)

    target_sources(OilSlick PRIVATE ${OILSLICK_SOURCES})
    target_include_directories(OilSlick PRIVATE Source)

    set(OILSLICK_DEFS
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_compile_definitions(OilSlick PUBLIC ${OILSLICK_DEFS})

    target_link_libraries(OilSlick
        PRIVATE
            OilSlickData
            oilslick_engine
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Footswitch test: the real processor, driven by plain MIDI press and release messages.
    if(OILSLICK_BUILD_TESTS)
        juce_add_console_app(oilslick_foot_test PRODUCT_NAME "oilslick_foot_test")
        target_sources(oilslick_foot_test PRIVATE Tests/FootTest.cpp ${OILSLICK_SOURCES})
        target_include_directories(oilslick_foot_test PRIVATE Source)
        target_compile_definitions(oilslick_foot_test PRIVATE ${OILSLICK_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(oilslick_foot_test
            PRIVATE
                OilSlickData
                oilslick_engine
                juce::juce_audio_utils
                juce::juce_dsp
            PUBLIC
                juce::juce_recommended_config_flags)
    endif()
endif()
