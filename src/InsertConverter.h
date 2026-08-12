#pragma once

#include "ConversionModification.h"
#include "ConversionSettings.h"
#include "VoiceLoader.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace rvcara
{

/** Voice conversion for hosts that load the plug-in as an ordinary insert, without ARA.

    RVC cannot convert a vocal as it streams past. Its content encoder is a transformer over
    the whole utterance, its pitch estimator sees the future, and its vocoder is conditioned
    on a pitch track that has been interpolated end to end. Measured on sixteen cores, a
    quarter-second window costs more than a quarter-second to convert; only past about a
    second of window does the cost fall to roughly half of real time, and by then the
    latency and the crossfade seams have already cost more quality than they save.

    So this does what an offline tool does, in insert form: **capture, then render, then play
    back in place.**

    1. The transport rolls and the audio thread copies what passes through into a buffer
       indexed by song position, while passing the dry signal to the output.
    2. Shortly after the transport stops, or when asked, the captured span is converted on a
       worker thread at full quality — the whole phrase in view, exactly as the ARA path
       does it.
    3. On the next pass the converted audio plays back at the same song positions, sample
       aligned, with no added latency and no seams.

    The cost is that the first pass is dry. The alternative — a streaming mode — would be
    worse in every dimension a mixing engineer cares about, and would still not be usable
    for live monitoring.

    ARA does all of this properly, because the host simply hands over the audio instead of
    the plug-in having to overhear it. This class exists for hosts that do not offer it.
*/
class InsertConverter final : private juce::Timer
{
public:
    InsertConverter();
    ~InsertConverter() override;

    InsertConverter (const InsertConverter&) = delete;
    InsertConverter& operator= (const InsertConverter&) = delete;

    /** How long a performance can be captured, in seconds.

        Buffers are allocated up front because the audio thread cannot allocate, and two
        are needed — the captured dry signal and the converted result. At 48 kHz that is
        about 23 MB per minute per instance. Five minutes covers a vocal take with room to
        spare; beyond it, capture stops extending and says so rather than reallocating
        underneath the audio thread.
    */
    static constexpr double maximumCaptureSeconds = 300.0;

    /** What the converter is doing. */
    enum class State
    {
        idle,        ///< Nothing captured yet
        capturing,   ///< Transport rolling, audio being taken in
        queued,      ///< Waiting for a worker
        rendering,
        ready,       ///< Converted audio is playing back
        failed
    };

    /** Allocates the capture buffers. Message thread only.

        @param sampleRate         Host sample rate.
        @param maximumBlockSize   Largest block the host will ask for.
    */
    void prepare (double sampleRate, int maximumBlockSize);

    /** Frees the buffers and abandons any render. */
    void release();

    /** Points the converter at the voice loader that owns the model. */
    void setVoiceLoader (VoiceLoader* loader) noexcept { voiceLoader = loader; }

    /** Replaces the conversion settings, invalidating the render if they changed. */
    void setSettings (const ConversionSettings& newSettings);

    /** @returns The settings currently applied. */
    [[nodiscard]] ConversionSettings getSettings() const;

    /** Captures input and writes converted output. **Audio thread.**

        Always leaves a usable signal in @c buffer: the conversion where one exists for these
        song positions, and the dry input everywhere else.

        @param buffer        Input on entry, output on return.
        @param positionInfo  The host's transport position. Without a valid position the
                             converter cannot align anything, so it passes audio through.
    */
    void process (juce::AudioBuffer<float>& buffer, const juce::AudioPlayHead::PositionInfo& positionInfo);

    /** Converts what has been captured so far. Message thread. */
    void requestConversion();

    /** Throws away the capture and the conversion, so a new take can be recorded over it. */
    void reset();

    [[nodiscard]] State getState() const noexcept { return state.load (std::memory_order_acquire); }
    [[nodiscard]] float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }

    /** @returns How much audio has been captured, in seconds. */
    [[nodiscard]] double getCapturedSeconds() const;

    /** @returns Whether there is captured audio that has not been converted with the
                current settings.
    */
    [[nodiscard]] bool isConversionStale() const;

    /** @returns The current conversion, for the editor's pitch display. */
    [[nodiscard]] ConversionPointer getConversion() const;

    /** @returns Why the last render failed, or an empty string. */
    [[nodiscard]] juce::String getError() const;

    /** @returns True once capture has filled the buffer and is dropping further audio. */
    [[nodiscard]] bool hasReachedCaptureLimit() const noexcept
    {
        return reachedLimit.load (std::memory_order_relaxed);
    }

private:
    /** Polls for the transport having stopped, which is the cue to convert.

        A timer rather than a transport callback because JUCE surfaces transport state only
        through the position info a block carries, so "stopped" can only be inferred from
        blocks having ceased to arrive.
    */
    void timerCallback() override;

    /** Publishes a finished render. Message thread. */
    void completeRender (ConversionPointer conversion, juce::String error);

    VoiceLoader* voiceLoader { nullptr };

    double hostSampleRate { 48000.0 };
    int numCaptureSamples { 0 };

    /** The dry capture and the converted result, both indexed by
        `songPositionInSamples - captureOriginInSamples`.

        Guarded by the voice loader's model lock: the audio thread takes it for reading and
        publishing a render takes it for writing.
    */
    std::vector<float> capturedAudio;
    std::vector<float> convertedAudio;

    /** Song position that maps to index zero of the buffers. */
    std::atomic<juce::int64> captureOrigin { 0 };
    std::atomic<bool> hasCaptureOrigin { false };

    std::atomic<int> firstCapturedSample { 0 };
    std::atomic<int> lastCapturedSample { 0 };
    std::atomic<bool> hasNewAudio { false };
    std::atomic<bool> reachedLimit { false };

    /** Set by the audio thread on every block, read by the timer to detect a stopped
        transport without having to ask the host.
    */
    std::atomic<juce::uint32> lastCaptureTimeMs { 0 };

    /** How long the transport must be quiet before a conversion starts.

        Long enough not to fire between the blocks of a loop boundary, short enough that the
        user does not wonder whether anything happened.
    */
    static constexpr int quietMillisecondsBeforeRender = 500;

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> shouldAbortRender { false };

    mutable juce::CriticalSection settingsLock;
    ConversionSettings settings;
    juce::String errorMessage;

    ConversionPointer conversion;

    /** Range of the conversion in buffer indices, valid while state is ready. */
    std::atomic<int> convertedFirstSample { 0 };
    std::atomic<int> convertedLastSample { 0 };

    class RenderJob;
    RenderJob* activeJob { nullptr };
};

} // namespace rvcara
