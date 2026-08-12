# The conversion engine, built as a static library separate from the plugin.
#
# Keeping it separate is what makes it testable. The engine knows nothing about ARA,
# about plugin formats, or about the message thread; it takes a buffer and returns a
# buffer. The unit tests link this target directly and never instantiate a plugin.

add_library(rvcara_engine STATIC
    dsp/MelSpectrogram.cpp
    dsp/SincResampler.cpp
    dsp/ZeroPhaseFilter.cpp
    engine/BinaryMatrix.cpp
    engine/ConversionEngine.cpp
    engine/FeatureRetriever.cpp
    engine/ModelManifest.cpp
    engine/OnnxSession.cpp
    engine/PitchEstimator.cpp
    engine/VoiceModel.cpp
    engine/VoiceModelLibrary.cpp)

add_library(rvcara::engine ALIAS rvcara_engine)

target_include_directories(rvcara_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(rvcara_engine
    PUBLIC
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_dsp
        rvcara::onnxruntime
    PRIVATE
        rvcara::hnswlib
        rvcara::warnings)

# JUCE_STANDALONE_APPLICATION is deliberately not set here. The plugin client defines it
# per format target, and defining it in the engine would redefine it for the standalone
# build.
target_compile_definitions(rvcara_engine
    PUBLIC
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        # The engine does no file dialogs, networking or web views; excluding them keeps
        # the static library and every binary that links it smaller.
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0)

# The DSP is where a silent float-to-double promotion or a C-style cast actually costs
# something, so it is held to the stricter set. FeatureRetriever is excluded: hnswlib's
# headers would fail it and are not ours to fix.
rvcara_apply_strict_warnings(
    dsp/MelSpectrogram.cpp
    dsp/SincResampler.cpp
    dsp/ZeroPhaseFilter.cpp
    engine/ConversionEngine.cpp
    engine/PitchEstimator.cpp)

# hnswlib's headers trip -Wall on unused parameters and sign comparisons in ways we
# cannot fix, and they are included by FeatureRetriever.cpp only.
if(NOT MSVC)
    set_property(SOURCE engine/FeatureRetriever.cpp APPEND PROPERTY COMPILE_OPTIONS
        -Wno-unused-parameter -Wno-sign-compare)
endif()

add_subdirectory(plugin)
