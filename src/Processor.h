#pragma once

#include "ara/ConversionModification.h"
#include "ara/DocumentController.h"
#include "insert/InsertConverter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace rvcara
{
/** @brief The plug-in: parameters, the voice loader, and the two paths audio can arrive by. */
class Processor final : public juce::AudioProcessor,
                              public juce::AudioProcessorARAExtension,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

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

    /** @brief Parameter identifiers, which the host persists in automation lanes. */
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

    [[nodiscard]] ConversionSettings getSettingsFromParameters() const;

    [[nodiscard]] std::vector<ConversionModification*> getEditableModifications() const;

    [[nodiscard]] DocumentController* getConversionDocumentController() const;

    [[nodiscard]] InsertConverter& getInsertConverter() noexcept { return insertConverter; }

    [[nodiscard]] VoiceLoader& getVoiceLoader() noexcept;

    /** @brief Returns true when the host has given this instance regions to render.

        False while bound to ARA means the host is not routing audio through the plug-in, so the
        track passes through unchanged however finished the conversion is.
    */
    [[nodiscard]] bool hasAssignedRegions() const;

    [[nodiscard]] bool isUsingARA() const { return getConversionDocumentController() != nullptr; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterId, float newValue) override;

    void convertCapturedAudio();

    juce::AudioProcessorValueTreeState parameters;

    VoiceLoader insertVoiceLoader;
    InsertConverter insertConverter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
}
