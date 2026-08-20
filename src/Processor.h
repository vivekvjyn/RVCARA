#pragma once

#include "ara/ConversionModification.h"
#include "ara/DocumentController.h"
#include "insert/InsertConverter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace rvcara
{
/** @brief The plug-in: parameters, the voice loader, and the two paths audio can arrive by. */
class Processor final : public juce::AudioProcessor,
                              public juce::AudioProcessorARAExtension,
                              private juce::AudioProcessorValueTreeState::Listener,
                              private juce::AsyncUpdater,
                              private juce::Timer
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

    [[nodiscard]] bool isUsingARA() const { return getConversionDocumentController() != nullptr; }

    /** @brief Where the transport is inside a region, so the editor can draw a playhead.
        @param modification  The region the editor is showing.
        @return The time in that region, or a time before zero when the transport is elsewhere.
    */
    [[nodiscard]] double getPlayheadInModification (const ConversionModification& modification) const;

    /** @brief Whether the host lets a plug-in drive its transport, which not all of them do. */
    [[nodiscard]] bool canControlTransport() const;

    /** @brief True while the host says it is playing. */
    [[nodiscard]] bool isTransportPlaying() const noexcept
    {
        return hostPositionSeconds.load (std::memory_order_relaxed) >= 0.0;
    }

    void setTransportPlaying (bool shouldPlay);

    void rewindTransport();

    /** @brief The host's tempo, or zero when it offers none. */
    [[nodiscard]] double getHostTempo() const noexcept { return hostTempo.load (std::memory_order_relaxed); }

    /** @brief The host's time signature numerator, or zero when it offers none. */
    [[nodiscard]] int getHostBeatsPerBar() const noexcept { return hostBeatsPerBar.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterId, float newValue) override;

    void handleAsyncUpdate() override;

    void timerCallback() override;

    void applySettings();

    void convertCapturedAudio();

    juce::AudioProcessorValueTreeState parameters;

    VoiceLoader insertVoiceLoader;
    InsertConverter insertConverter;

    std::atomic<double> hostPositionSeconds { -1.0 };
    std::atomic<double> hostTempo { 0.0 };
    std::atomic<int> hostBeatsPerBar { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
}
