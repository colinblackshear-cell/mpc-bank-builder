cmake_minimum_required(VERSION 3.24)

project(MpcBankBuilder VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(MPC_TESTS_ONLY "Build only the file format tests (skips downloading JUCE)" OFF)

# The MPC file formats, with no JUCE dependency.
add_library(mpc_files STATIC
    Source/mpc/MpcFiles.cpp
    Source/mpc/BankWriter.cpp)
target_include_directories(mpc_files PUBLIC Source)

enable_testing()
add_executable(format_tests tests/FormatTests.cpp)
target_link_libraries(format_tests PRIVATE mpc_files)
add_test(NAME format_tests COMMAND format_tests)

if(MPC_TESTS_ONLY)
    return()
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# Time-stretching and pitch-shifting (MIT licence).
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.4.0
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(signalsmith-stretch)

set(APP_SOURCES
    Source/app/AudioEngine.cpp
    Source/app/BankModel.cpp
    Source/app/KitPage.cpp
    Source/app/MainComponent.cpp
    Source/app/PadEditor.cpp
    Source/app/PadGrid.cpp
    Source/app/SampleTools.cpp
    Source/app/SamplerPage.cpp
    Source/app/SoundFx.cpp
    Source/app/SoundLibrary.cpp
    Source/app/WaveEditor.cpp)

juce_add_gui_app(MpcBankBuilder
    COMPANY_NAME "Colin Blackshear"
    BUNDLE_ID "com.colinblackshear.mpcbankbuilder"
    PRODUCT_NAME "MPC Bank Builder"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "MPC Bank Builder records from your audio input when you sample.")

target_sources(MpcBankBuilder PRIVATE Source/app/Main.cpp ${APP_SOURCES})

set(APP_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MpcBankBuilder,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MpcBankBuilder,JUCE_VERSION>")

target_compile_definitions(MpcBankBuilder PRIVATE ${APP_DEFINITIONS})

target_link_libraries(MpcBankBuilder
    PRIVATE
        mpc_files
        signalsmith-stretch
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless test of the app's audio side: load audio files, render pads and export a bank.
juce_add_console_app(AppTests PRODUCT_NAME "AppTests")
target_sources(AppTests PRIVATE tests/AppTests.cpp ${APP_SOURCES})
target_compile_definitions(AppTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="AppTests"
    JUCE_APPLICATION_VERSION_STRING="0.1.0")
target_link_libraries(AppTests PRIVATE
    mpc_files
    signalsmith-stretch
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME app_tests COMMAND AppTests)
