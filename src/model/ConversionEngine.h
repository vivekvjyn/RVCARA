#pragma once

#include "common/ConversionSettings.h"
#include "common/PitchEdit.h"
#include "model/VoiceModel.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <vector>

namespace rvcara
{
/** @brief Converts a whole performance through a voice: resample, pitch, content, retrieval, vocoder. */
class ConversionEngine
{
public:
    explicit ConversionEngine (const VoiceModel& model);

    /** @brief What to convert. */
    struct Request
    {
        const float* samples { nullptr };
        int numSamples { 0 };
        double sampleRate { 44100.0 };

        /** @brief The rate to render at. Zero renders at @ref sampleRate. */
        double outputSampleRate { 0.0 };

        ConversionSettings settings;

        /** @brief The pitch edit to sing, or nullptr to sing the take as it was sung. */
        const PitchEdit* pitchEdit { nullptr };
    };

    /** @brief The outcome of a conversion. */
    struct Result
    {
        std::vector<float> samples;

        std::vector<float> fundamentalFrequencyHz;

        /** @brief The melody as it was sung, before the edit and the transposition moved it. */
        std::vector<float> sourceFundamentalFrequencyHz;

        double pitchFrameRate { 100.0 };

        bool isValid { false };
        juce::String error;
    };

    using ProgressCallback = std::function<void (float)>;

    /** @brief Receives the conversion so far, so the caller can play and show it while the rest
               is still rendering. Its samples cover the region from the start.
    */
    using PartialCallback = std::function<void (Result)>;

    [[nodiscard]] Result convert (const Request& request,
                                  const ProgressCallback& onProgress,
                                  const std::atomic<bool>& shouldAbort,
                                  const PartialCallback& onPartial = {}) const;

    [[nodiscard]] int getMinimumNumSamples (double sampleRate) const noexcept;

private:
    struct Chunk
    {
        int startSample { 0 };
        int endSample { 0 };
        int startFrame { 0 };
        int endFrame { 0 };
    };

    [[nodiscard]] std::vector<Chunk> planChunks (const float* filtered,
                                                 int numFilteredSamples,
                                                 int numPaddedSamples,
                                                 int numFrames) const;

    [[nodiscard]] std::vector<float> encodeContent (const float* samples,
                                                   int numSamples,
                                                   juce::String& error) const;

    [[nodiscard]] std::vector<float> synthesise (const std::vector<float>& contentFeatures,
                                                const std::int64_t* coarsePitch,
                                                const float* fundamentalFrequencyHz,
                                                int numFrames,
                                                const ConversionSettings& settings,
                                                juce::String& error) const;

    void followSourceEnvelope (std::vector<float>& converted,
                               const float* source,
                               int numSourceSamples,
                               double sourceSampleRate,
                               float ratio) const;

    const VoiceModel& voiceModel;
    const ModelManifest& manifest;
};
}
