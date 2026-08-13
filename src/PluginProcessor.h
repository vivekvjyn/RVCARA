#pragma once

#include "ConversionModification.h"
#include "DocumentController.h"
#include "InsertConverter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace rvcara
{

/** The plugin.

    Thin by design. RVCARA does no processing of its own on the audio thread, because the
    model is non-causal and needs the whole performance before it can produce a single
    sample. Both paths therefore serve audio out of a cache, and this class exists to route
    to whichever is in use, to hold the parameters, and to own the editor.

    - **Bound to ARA**, the document controller reads each region through the host and the
      playback renderer serves the conversion.
    - **Loaded as an ordinary insert**, InsertConverter captures what passes through, converts
      it once the transport stops, and plays the conversion back at the same song positions on
      the next pass. See its header for why streaming is not an option.

    Either way the voice loads by itself: prepareToPlay asks for the first installed voice, so
    the only thing the user has to do is play the track. Loading is deliberately not done in
    the constructor — hosts instantiate plug-ins while scanning, and a scan should not read a
    gigabyte of graph off the disk.

    Parameters exist so the host can automate and display the controls, but in ARA mode the
    model is the authority: the values are pushed into the audio modifications of whichever
    regions this instance renders, and the editor reads state back from there.
*/
class PluginProcessor final : public juce::AudioProcessor,
                              public juce::AudioProcessorARAExtension,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    // ==============================================================================
    // AudioProcessor

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** Keeps the double-precision overload visible.

        Declaring only the float one hides `processBlock (AudioBuffer<double>&, MidiBuffer&)`.
        Nothing calls it — supportsDoublePrecisionProcessing() is false, and the conversion is
        float throughout — but a hidden virtual is how a host silently gets a no-op instead of
        the processing it asked for, so the base's version is named rather than shadowed.
    */
    using juce::AudioProcessor::processBlock;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ==============================================================================
    // Parameters

    /** Parameter identifiers. Strings rather than an enum because the host persists them
        in automation lanes, so they are part of the plugin's compatibility surface.
    */
    struct ParameterId
    {
        static constexpr auto pitchShiftSemitones = "pitchShiftSemitones";
        static constexpr auto retrievalRatio      = "retrievalRatio";
        static constexpr auto consonantProtection = "consonantProtection";
        static constexpr auto envelopeFollowRatio = "envelopeFollowRatio";
        static constexpr auto latentNoiseSeed     = "latentNoiseSeed";
        static constexpr auto isBypassed          = "isBypassed";
    };

    [[nodiscard]] juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    /** @returns The settings the parameters currently describe. */
    [[nodiscard]] ConversionSettings getSettingsFromParameters() const;

    /** Copies a modification's settings into the parameters, without echoing back.

        Used when the editor's selection changes so the controls show what the selected
        region is actually using.
    */
    void setParametersFromSettings (const ConversionSettings& settings);

    /** @returns The modifications this instance renders, or every modification in the
                document if the host has not assigned this instance any regions yet.
    */
    [[nodiscard]] std::vector<ConversionModification*> getEditableModifications() const;

    /** @returns Our ARA document controller, or nullptr when not bound to ARA. */
    [[nodiscard]] DocumentController* getConversionDocumentController() const;

    /** @returns The insert-mode converter, used when the host offers no ARA. */
    [[nodiscard]] InsertConverter& getInsertConverter() noexcept { return insertConverter; }

    /** @returns The voice loader in use, whichever mode the plug-in is running in. */
    [[nodiscard]] VoiceLoader& getVoiceLoader() noexcept;

    /** @returns True when the host has bound this instance to ARA. */
    [[nodiscard]] bool isUsingARA() const { return getConversionDocumentController() != nullptr; }

private:
    /** Builds the parameter layout. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterId, float newValue) override;

    /** Re-converts the insert-mode capture, if there is one. */
    void convertCapturedAudio();

    juce::AudioProcessorValueTreeState parameters;

    /** Voice ownership for insert mode. Unused when ARA is active, where the document
        controller owns the loader instead so that every instance in the session shares one.
    */
    VoiceLoader insertVoiceLoader;
    InsertConverter insertConverter;

    /** Suppresses the parameter listener while the editor pushes state in, so that
        showing a region's settings does not immediately write them back and requeue a
        render.
    */
    bool isSynchronisingParameters { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace rvcara
