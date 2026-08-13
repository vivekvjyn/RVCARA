#pragma once

#include "common/ConversionSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace rvcara
{
/** @brief A finished conversion: the audio, the pitch track it followed, and what produced it. */
struct ConversionResult
{
    std::vector<float> samples;
    std::vector<float> fundamentalFrequencyHz;
    double pitchFrameRate { 100.0 };

    ConversionSettings settings;
    juce::String voiceName;

    [[nodiscard]] int getNumSamples() const noexcept { return static_cast<int> (samples.size()); }
};

using ConversionPointer = std::shared_ptr<const ConversionResult>;

/** @brief ARA audio modification holding one region's settings, state and cached conversion. */
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

    [[nodiscard]] const ConversionSettings& getSettings() const noexcept { return settings; }

    bool setSettings (const ConversionSettings& newSettings);

    [[nodiscard]] const juce::String& getVoiceName() const noexcept { return voiceName; }

    bool setVoiceName (const juce::String& name);

    [[nodiscard]] ConversionPointer getConversion() const noexcept { return conversion; }

    void setConversion (ConversionPointer newConversion);

    void clearConversion();

    [[nodiscard]] bool isConversionCurrent() const noexcept;

    [[nodiscard]] State getState() const noexcept { return state.load (std::memory_order_acquire); }
    void setState (State newState) noexcept { state.store (newState, std::memory_order_release); }

    [[nodiscard]] float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }
    void setProgress (float newProgress) noexcept { progress.store (newProgress, std::memory_order_relaxed); }

    [[nodiscard]] juce::String getError() const;
    void setError (const juce::String& message);

    void writeToArchive (juce::OutputStream& stream) const;

    bool readFromArchive (juce::InputStream& stream);

    static void readAndDiscard (juce::InputStream& stream);

private:
    static constexpr int archiveVersion = 1;

    ConversionSettings settings;
    juce::String voiceName;

    ConversionPointer conversion;

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };

    mutable juce::CriticalSection errorLock;
    juce::String errorMessage;
};
}
