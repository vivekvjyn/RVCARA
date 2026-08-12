#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>

namespace rvcara::ara
{

class DocumentController;

/** Plays the converted audio back along the host's timeline.

    Unlike a normal effect's processBlock, nothing is computed here. The conversion was
    rendered minutes ago on a worker thread; this walks each playback region, works out
    which part of the conversion the current block corresponds to, and copies. That is
    the whole point of the ARA arrangement: the expensive non-causal model runs once,
    offline, with the entire performance visible to it, and playback is a memory read.

    Two behaviours are worth stating because they are choices, not accidents:

    - **A region with no finished conversion plays the source audio dry.** The
      alternative — silence until a two-minute render completes — makes the plugin feel
      broken while it is working correctly. The editor is where the user learns that a
      render is pending.
    - **A stale conversion is played rather than suppressed.** After a control change the
      previous render keeps playing until the new one lands, so dragging a slider does not
      punch holes in playback.
*/
class PlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    /** @param documentController  The ARA document controller, as ARA supplies it.
        @param owner               Our specialisation, for the processing lock.
    */
    PlaybackRenderer (ARA::PlugIn::DocumentController* documentController, DocumentController& owner);

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
    /** A reader for one audio source, buffered when the host may ask in real time.

        Reading a file on the audio thread is only acceptable if something else is doing
        the waiting, which is what BufferingAudioReader and its shared background thread
        are for. During an offline bounce the host tolerates blocking, so the plain reader
        is used and no thread is needed.
    */
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

    /** Reads the dry source audio for a region into the output buffer. */
    bool renderSourceAudio (juce::ARAPlaybackRegion& playbackRegion,
                            juce::AudioBuffer<float>& buffer,
                            int startInBuffer,
                            int numSamplesToRead,
                            juce::int64 startInSource,
                            juce::AudioProcessor::Realtime realtime);

    DocumentController& owner;

    /** The background thread the buffering readers share.

        TimeSliceThread has no default constructor, so it needs this wrapper to be usable
        with SharedResourcePointer — which is what makes many plugin instances in a session
        share one read-ahead thread rather than start one each.
    */
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
    int numOutputChannels { 2 };
    int maximumBlockSize { 512 };
    bool isAlwaysNonRealtime { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaybackRenderer)
};

} // namespace rvcara::ara
