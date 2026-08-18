#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>

namespace rvcara
{
class DocumentController;

/** @brief Serves converted audio to the host for the regions this instance renders. */
class PlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    PlaybackRenderer (ARA::PlugIn::DocumentController* documentController, DocumentController& owner);

    ~PlaybackRenderer() override;

    void prepareToPlay (double sampleRate,
                        int maximumSamplesPerBlock,
                        int numChannels,
                        juce::AudioProcessor::ProcessingPrecision precision,
                        AlwaysNonRealtime alwaysNonRealtime) override;

    void releaseResources() override;

    bool processBlock (juce::AudioBuffer<float>& buffer,
                       juce::AudioProcessor::Realtime realtime,
                       const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    using juce::ARAPlaybackRenderer::processBlock;

private:
    struct SourceReader
    {
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::BufferingAudioReader* buffering { nullptr };

        void setReadTimeout (int milliseconds) const
        {
            if (buffering != nullptr)
                buffering->setReadTimeout (milliseconds);
        }
    };

    bool renderSourceAudio (juce::ARAPlaybackRegion& playbackRegion,
                            juce::AudioBuffer<float>& buffer,
                            int startInBuffer,
                            int numSamplesToRead,
                            juce::int64 startInSource,
                            juce::AudioProcessor::Realtime realtime);

    DocumentController& owner;

    struct ReadAheadThread final : public juce::TimeSliceThread
    {
        ReadAheadThread() : juce::TimeSliceThread ("RVCARA read ahead")
        {
            startThread (juce::Thread::Priority::normal);
        }

        ~ReadAheadThread() override { stopThread (1000); }
    };

    juce::SharedResourcePointer<ReadAheadThread> readAheadThread;

    std::map<juce::ARAAudioSource*, SourceReader> sourceReaders;

    double hostSampleRate { 48000.0 };
    bool isAlwaysNonRealtime { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaybackRenderer)
};
}
