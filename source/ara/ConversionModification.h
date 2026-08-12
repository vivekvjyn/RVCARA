#pragma once

#include <engine/ConversionSettings.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace rvcara::ara
{

/** A finished conversion, immutable once published.

    Shared by pointer between the thread that produced it and the renderer that reads
    it, so it carries the settings and voice that produced it: that is what lets the
    renderer, the editor and the cache-invalidation logic all agree on whether what
    they are looking at is still current, without consulting anything else.
*/
struct ConversionResult
{
    std::vector<float> samples;                 ///< Mono, at the audio source's sample rate
    std::vector<float> fundamentalFrequencyHz;  ///< The melody, for the editor's overlay
    double pitchFrameRate { 100.0 };

    engine::ConversionSettings settings;        ///< What produced this
    juce::String voiceName;

    [[nodiscard]] int getNumSamples() const noexcept { return static_cast<int> (samples.size()); }
};

using ConversionPointer = std::shared_ptr<const ConversionResult>;

/** ARA's audio modification, extended with the conversion of that audio.

    In ARA's model an audio modification is "the user's edit of an audio source" — the
    thing that persists in the session document and that several playback regions can
    share. For RVCARA that edit is: which voice, and the five controls. So this class
    owns the settings, owns the rendered result, and is what gets archived.

    Threading. The settings are written from the message thread and read by the render
    worker; the result is published by the worker and read by the audio thread. Neither
    is protected here. Both are protected by the document controller's processing lock,
    which is the same discipline ARA itself imposes for model access, and centralising it
    there is what keeps a render swap from tearing against a block that is mid-read.
*/
class ConversionModification final : public juce::ARAAudioModification
{
public:
    using juce::ARAAudioModification::ARAAudioModification;

    /** How far along this modification's conversion is. */
    enum class State
    {
        idle,       ///< Nothing requested yet, or no voice chosen
        queued,     ///< Waiting for a worker
        rendering,
        ready,
        failed
    };

    /** @returns The controls currently applied to this modification. */
    [[nodiscard]] const engine::ConversionSettings& getSettings() const noexcept { return settings; }

    /** Replaces the controls.

        @param newSettings  The new controls.
        @returns            True if anything actually changed, meaning the cached
                            conversion is now stale and a re-render is needed.
    */
    bool setSettings (const engine::ConversionSettings& newSettings);

    /** @returns The voice this modification renders through, empty if none is chosen. */
    [[nodiscard]] const juce::String& getVoiceName() const noexcept { return voiceName; }

    /** Chooses the voice.

        @param name  Voice name as the library reports it.
        @returns     True if the choice changed.
    */
    bool setVoiceName (const juce::String& name);

    /** @returns The current conversion, or nullptr when none has been published. */
    [[nodiscard]] ConversionPointer getConversion() const noexcept { return conversion; }

    /** Publishes a finished conversion. Call under the processing lock. */
    void setConversion (ConversionPointer newConversion);

    /** Discards the cached conversion. Call under the processing lock. */
    void clearConversion();

    /** @returns Whether the cached conversion matches the current voice and settings.

        A conversion that does not match is still played — a slightly stale voice is far
        better than silence while a two-minute render completes — but the editor shows it
        as out of date and the render queue will replace it.
    */
    [[nodiscard]] bool isConversionCurrent() const noexcept;

    [[nodiscard]] State getState() const noexcept { return state.load (std::memory_order_acquire); }
    void setState (State newState) noexcept { state.store (newState, std::memory_order_release); }

    /** @returns Render progress in [0, 1]; meaningful while rendering. */
    [[nodiscard]] float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }
    void setProgress (float newProgress) noexcept { progress.store (newProgress, std::memory_order_relaxed); }

    /** @returns Why the last render failed, or an empty string. */
    [[nodiscard]] juce::String getError() const;
    void setError (const juce::String& message);

    /** Writes the settings and voice choice into a session archive.

        The rendered audio is deliberately *not* archived. It is tens of megabytes per
        region, it is reproducible from the settings and the seed, and a session that
        carried it would bloat without bound. Reopening a session re-renders.
    */
    void writeToArchive (juce::OutputStream& stream) const;

    /** Reads settings and voice choice back from an archive.

        @returns False if the stream holds a format this build does not understand, in
                 which case the modification keeps its defaults rather than adopting
                 half-read values.
    */
    bool readFromArchive (juce::InputStream& stream);

    /** Consumes one archived record without applying it.

        Needed when an archive holds a record for a modification the document being
        restored into does not have. Skipping the record keeps the stream aligned so the
        remaining records still restore, instead of abandoning the whole archive.

        @param stream  Stream positioned at the start of a record.
    */
    static void readAndDiscard (juce::InputStream& stream);

private:
    /** Archive format version. Bump when the layout below changes. */
    static constexpr int archiveVersion = 1;

    engine::ConversionSettings settings;
    juce::String voiceName;

    ConversionPointer conversion;

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };

    mutable juce::CriticalSection errorLock;
    juce::String errorMessage;
};

} // namespace rvcara::ara
