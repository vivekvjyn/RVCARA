# The plugin target.
#
# IS_ARA_EFFECT is what turns this from an ordinary VST3/AU into one a compatible host can
# load with timeline access. It defines JucePlugin_Enable_ARA, enables the ARA code paths
# in JUCE's VST3 and AU wrappers, and requires createARAFactory() alongside the usual
# createPluginFilter().
#
# The two ARA identifiers below are part of the plugin's compatibility surface and must be
# updated deliberately:
#
#   ARA_FACTORY_ID          must change when the analysis or playback capabilities change,
#                           or when the archive ID changes.
#   ARA_DOCUMENT_ARCHIVE_ID must change when the archive format changes in a way older
#                           versions cannot read. It is shared only between builds that
#                           produce identical renders from identical input, which is why
#                           it carries a version.
#
# ARA_ANALYSIS_TYPES is deliberately left at its default of none. RVCARA does estimate
# pitch, but it does not expose notes, tempo or key to the host as readable content, and
# claiming a capability the document controller does not implement would have hosts
# querying for content readers that do not exist.

juce_add_plugin(RVCARA
    PLUGIN_MANUFACTURER_CODE Pinn
    PLUGIN_CODE Rvca
    MANUFACTURER_NAME "Pitch Innovations"
    MANUFACTURER_WEBSITE "https://github.com/vivekvjyn/RVCARA"
    COMPANY_NAME "Pitch Innovations"
    BUNDLE_ID "com.pitchinnovations.rvcara"
    PRODUCT_NAME "RVCARA"
    DESCRIPTION "Retrieval-based voice conversion as an ARA plug-in"

    FORMATS AU VST3 Standalone
    IS_ARA_EFFECT TRUE
    # The escaped quotes are required: JUCE passes these straight through into a
    # preprocessor define, so the value has to carry its own string literal quotes.
    ARA_FACTORY_ID "\"com.pitchinnovations.rvcara.arafactory.0.1.0\""
    ARA_DOCUMENT_ARCHIVE_ID "\"com.pitchinnovations.rvcara.aradocumentarchive.1\""

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE)

target_sources(RVCARA PRIVATE
    PluginEditor.cpp
    PluginProcessor.cpp
    ../ara/ConversionModification.cpp
    ../ara/DocumentController.cpp
    ../ara/PlaybackRenderer.cpp
    ../gui/PitchCurveView.cpp)

target_include_directories(RVCARA PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_definitions(RVCARA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # The editor polls the model on a timer instead of relying on modal loops, and modal
    # loops are not permitted inside a plugin in any case.
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(RVCARA
    PRIVATE
        rvcara::engine
        rvcara::warnings
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

rvcara_apply_strict_warnings(
    PluginEditor.cpp
    PluginProcessor.cpp
    ../ara/ConversionModification.cpp
    ../ara/DocumentController.cpp
    ../ara/PlaybackRenderer.cpp
    ../gui/PitchCurveView.cpp)

# Every format target needs the ONNX Runtime shared library beside it, because a plugin
# cannot rely on the host's library search path.
foreach(format Standalone VST3 AU)
    if(TARGET RVCARA_${format})
        rvcara_copy_onnxruntime_beside(RVCARA_${format})
    endif()
endforeach()
