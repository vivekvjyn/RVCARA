#pragma once

#include "common/ConversionResult.h"
#include "common/ConversionSettings.h"
#include "common/PitchEdit.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace rvcara
{

/** @brief ARA audio modification holding one region's settings, notes, state and cached conversion. */
class ConversionModification final : public juce::ARAAudioModification
{
public:
    using juce::ARAAudioModification::ARAAudioModification;

    enum class State
    {
        idle,
        queued,
        rendering,
        ready,
        failed
    };

    /** @brief How far note detection has got, which the panel reports. */
    enum class NoteState
    {
        none,
        finding,
        found,
        failed
    };

    [[nodiscard]] const ConversionSettings& getSettings() const noexcept { return settings; }

    bool setSettings (const ConversionSettings& newSettings);

    [[nodiscard]] const juce::String& getVoiceName() const noexcept { return voiceName; }

    bool setVoiceName (const juce::String& name);

    /** @brief The notes the region was split into, and what the user has done to them. */
    [[nodiscard]] PitchEdit getPitchEdit() const;

    /** @brief Replaces the edit.
        @param newEdit  The notes to keep.
        @return True when this changed anything, so the caller knows to re-render.
    */
    bool setPitchEdit (PitchEdit newEdit);

    [[nodiscard]] bool hasNotes() const;

    [[nodiscard]] NoteState getNoteState() const noexcept { return noteState.load (std::memory_order_acquire); }
    void setNoteState (NoteState newState) noexcept { noteState.store (newState, std::memory_order_release); }

    [[nodiscard]] ConversionPointer getConversion() const noexcept { return conversion; }

    void setConversion (ConversionPointer newConversion);

    void clearConversion();

    /** @brief Whether the cached conversion still matches the voice, the settings, the edit and the rate.
        @param sampleRate  The rate the conversion must have been rendered at to be playable.
        @return True when the cache may be served without re-rendering.
    */
    [[nodiscard]] bool isConversionCurrent (double sampleRate) const;

    [[nodiscard]] State getState() const noexcept { return state.load (std::memory_order_acquire); }
    void setState (State newState) noexcept { state.store (newState, std::memory_order_release); }

    [[nodiscard]] float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }
    void setProgress (float newProgress) noexcept { progress.store (newProgress, std::memory_order_relaxed); }

    [[nodiscard]] juce::String getError() const;
    void setError (const juce::String& message);

    /** @brief Why note detection failed, which is reported apart from a conversion failure. */
    [[nodiscard]] juce::String getNoteError() const;
    void setNoteError (const juce::String& message);

    void writeToArchive (juce::OutputStream& stream) const;

    bool readFromArchive (juce::InputStream& stream);

    static void readAndDiscard (juce::InputStream& stream);

private:
    /** @brief One held the settings, two the notes, three their tilts, four the drawn curve. */
    static constexpr int archiveVersion = 4;

    static void writeNotes (juce::OutputStream& stream, const PitchEdit& edit);

    static PitchEdit readNotes (juce::InputStream& stream, int version);

    ConversionSettings settings;
    juce::String voiceName;

    mutable juce::CriticalSection editLock;
    PitchEdit pitchEdit;

    ConversionPointer conversion;

    std::atomic<State> state { State::idle };
    std::atomic<NoteState> noteState { NoteState::none };
    std::atomic<float> progress { 0.0f };

    mutable juce::CriticalSection errorLock;
    juce::String errorMessage;
    juce::String noteErrorMessage;
};
}
