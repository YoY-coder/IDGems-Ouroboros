cmake_minimum_required(VERSION 3.22)

# Runtime C++ statique sous Windows : aucun "redistributable" à installer.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(IDGemsOuroboros VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE : soit un dossier local (-DJUCE_DIR=...), soit téléchargé automatiquement.
set(JUCE_DIR "" CACHE PATH "Dossier JUCE local (optionnel)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(IDGemsOuroboros
    COMPANY_NAME "IDGems"
    PLUGIN_MANUFACTURER_CODE IDGm
    PLUGIN_CODE Ouro
    FORMATS VST3
    PRODUCT_NAME "IDGems Ouroboros"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

file(GLOB_RECURSE OURO_SOURCES CONFIGURE_DEPENDS source/*.cpp source/*.h)
target_sources(IDGemsOuroboros PRIVATE ${OURO_SOURCES})

target_include_directories(IDGemsOuroboros PRIVATE source)

target_compile_definitions(IDGemsOuroboros PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1)

target_link_libraries(IDGemsOuroboros
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
