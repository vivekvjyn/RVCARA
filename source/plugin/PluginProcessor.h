#pragma once

#include <ara/ConversionModification.h>
#include <ara/DocumentController.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace rvcara::plugin
{

/** The plugin.

    Thin by design. RVCARA does no processing of its own: when bound to ARA the playback
    renderer serves audio out of a cache, and when not bound there is nothing meaningful
    to do, because the model is non-causal and needs the whole performance before it can
    produce a single sample. So this class exists to hold parameters, to bridge them to
    the ARA model, and to own the editor.

    **Not bound to ARA, the plugin passes audio through unchanged.** That is the honest
    behaviour rather than a limitation to work around. A streaming implementation would
    need a sliding window with hundreds of milliseconds of latency and would sound
    materially worse; presenting that as the same product would be misleading. The editor
    says so plainly when the host has loaded the plugin without ARA.

    Parameters exist so the host can automate and display the controls, but the ARA model
    is the authority: the values are pushed into the audio modifications of whichever
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
    [[nodiscard]] engine::ConversionSettings getSettingsFromParameters() const;

    /** Copies a modification's settings into the parameters, without echoing back.

        Used when the editor's selection changes so the controls show what the selected
        region is actually using.
    */
    void setParametersFromSettings (const engine::ConversionSettings& settings);

    /** @returns The modifications this instance renders, or every modification in the
                document if the host has not assigned this instance any regions yet.
    */
    [[nodiscard]] std::vector<ara::ConversionModification*> getEditableModifications() const;

    /** @returns Our ARA document controller, or nullptr when not bound to ARA. */
    [[nodiscard]] ara::DocumentController* getConversionDocumentController() const;

private:
    /** Builds the parameter layout. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterId, float newValue) override;

    juce::AudioProcessorValueTreeState parameters;

    /** Suppresses the parameter listener while the editor pushes state in, so that
        showing a region's settings does not immediately write them back and requeue a
        render.
    */
    bool isSynchronisingParameters { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace rvcara::plugin
