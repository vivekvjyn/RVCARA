#pragma once

#include "ara/ConversionModification.h"
#include "common/ConversionSettings.h"
#include "model/VoiceLoader.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace rvcara
{
/** @brief Captures, converts and plays back in place, for hosts that offer no ARA. */
class InsertConverter final : private juce::Timer
{
public:
    InsertConverter();
    ~InsertConverter() override;

    InsertConverter (const InsertConverter&) = delete;
    InsertConverter& operator= (const InsertConverter&) = delete;

    static constexpr double maximumCaptureSeconds = 300.0;

    /** @brief What the converter is doing. */
    enum class State
    {
        idle,
        capturing,
        queued,
        rendering,
        ready,
        failed
    };

    void prepare (double sampleRate, int maximumBlockSize);

    void release();

    void setVoiceLoader (VoiceLoader* loader) noexcept { voiceLoader = loader; }

    void setSettings (const ConversionSettings& newSettings);

    [[nodiscard]] ConversionSettings getSettings() const;

    void process (juce::AudioBuffer<float>& buffer, const juce::AudioPlayHead::PositionInfo& positionInfo);

    void requestConversion();

    void reset();

    [[nodiscard]] State getState() const noexcept { return state.load (std::memory_order_acquire); }
    [[nodiscard]] float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }

    [[nodiscard]] double getCapturedSeconds() const;

    [[nodiscard]] bool isConversionStale() const;

    [[nodiscard]] ConversionPointer getConversion() const;

    [[nodiscard]] juce::String getError() const;

    [[nodiscard]] bool hasReachedCaptureLimit() const noexcept
    {
        return reachedLimit.load (std::memory_order_relaxed);
    }

    /** @brief Returns true once any audio has reached the plug-in. */
    [[nodiscard]] bool hasReceivedAudio() const noexcept
    {
        return receivedAudio.load (std::memory_order_relaxed);
    }

    /** @brief Returns true once the host has given a transport position to align against. */
    [[nodiscard]] bool hasHostTimeline() const noexcept
    {
        return sawTransport.load (std::memory_order_relaxed);
    }

private:
    void timerCallback() override;

    void completeRender (ConversionPointer conversion, juce::String error);

    VoiceLoader* voiceLoader { nullptr };

    double hostSampleRate { 48000.0 };
    int numCaptureSamples { 0 };

    std::vector<float> capturedAudio;
    std::vector<float> convertedAudio;

    std::atomic<juce::int64> captureOrigin { 0 };
    std::atomic<bool> hasCaptureOrigin { false };

    std::atomic<int> firstCapturedSample { 0 };
    std::atomic<int> lastCapturedSample { 0 };
    std::atomic<bool> hasNewAudio { false };
    std::atomic<bool> reachedLimit { false };
    std::atomic<bool> receivedAudio { false };
    std::atomic<bool> sawTransport { false };

    std::atomic<juce::uint32> lastCaptureTimeMs { 0 };

    static constexpr int quietMillisecondsBeforeRender = 500;

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> shouldAbortRender { false };

    mutable juce::CriticalSection settingsLock;
    ConversionSettings settings;
    juce::String errorMessage;

    ConversionPointer conversion;

    std::atomic<int> convertedFirstSample { 0 };
    std::atomic<int> convertedLastSample { 0 };

    class RenderJob;
    RenderJob* activeJob { nullptr };
};
}
