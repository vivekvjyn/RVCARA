#pragma once

#include "ConversionSettings.h"
#include "VoiceModel.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <vector>

namespace rvcara
{

/** Converts a performance through a voice model.

    The whole pipeline, in one place, transcribed from `infer/vc/pipeline.py` and from
    `tools/rvcara_export/pipeline.py` — the latter being the NumPy implementation that
    was checked against the reference render before any of this was written. When this
    and the reference disagree, that Python file is the arbiter.

    The result is **mono at the source's own sample rate**, not at the model's. That
    choice matters for ARA: the renderer needs to map a song-time sample range onto a
    position in the conversion, and a cache at the source rate makes that mapping the
    identity. Converting 40 kHz up to the source rate here, once, is cheaper and less
    error-prone than doing rate arithmetic on every block.

    Nothing here touches the audio thread. A conversion of a three-minute vocal takes
    around two minutes of CPU, so it runs on a background thread, reports progress, and
    can be abandoned mid-render when the user changes a setting.
*/
class ConversionEngine
{
public:
    /** @param model  The voice to convert through; must outlive this engine. */
    explicit ConversionEngine (const VoiceModel& model);

    /** What to convert. */
    struct Request
    {
        const float* samples { nullptr };  ///< Mono source audio
        int numSamples { 0 };
        double sampleRate { 44100.0 };     ///< The source's rate, which the result matches
        ConversionSettings settings;
    };

    /** The outcome of a conversion. */
    struct Result
    {
        /** Mono conversion at the request's sample rate, the same length as the input. */
        std::vector<float> samples;

        /** The pitch track, at the conditioning rate, for the editor's overlay.

            Kept because it is free — the estimator produced it anyway — and drawing the
            melody the model actually followed is the single most useful thing the
            interface can show about why a render sounds the way it does.
        */
        std::vector<float> fundamentalFrequencyHz;

        /** Frames per second of @c fundamentalFrequencyHz. */
        double pitchFrameRate { 100.0 };

        bool isValid { false };
        juce::String error;
    };

    /** Progress reporting, called from the converting thread with a value in [0, 1]. */
    using ProgressCallback = std::function<void (float)>;

    /** Converts a performance.

        @param request      What to convert.
        @param onProgress   Called with fractional progress; may be empty.
        @param shouldAbort  Polled between chunks and between stages; when it becomes
                            true the conversion returns an invalid result promptly.
        @returns            The conversion, or an invalid result with an error set.
    */
    [[nodiscard]] Result convert (const Request& request,
                                  const ProgressCallback& onProgress,
                                  const std::atomic<bool>& shouldAbort) const;

    /** @returns The shortest input worth converting, in samples at the given rate.

        Below this the content encoder has too little to work with and the spectrogram
        cannot be reflected, so a conversion would fail rather than sound bad.
    */
    [[nodiscard]] int getMinimumNumSamples (double sampleRate) const noexcept;

private:
    /** One span of the padded analysis signal, and the frames it covers. */
    struct Chunk
    {
        int startSample { 0 };
        int endSample { 0 };     ///< exclusive
        int startFrame { 0 };
        int endFrame { 0 };      ///< exclusive
    };

    /** Splits the padded signal so no chunk exceeds the model's maximum length.

        Seams are placed at the quietest point within a search window of each nominal
        boundary, so a join lands in a breath rather than mid-vowel, and every chunk
        carries the full context padding on both sides — which is then trimmed from its
        output, so the pieces abut without needing a cross-fade.
    */
    [[nodiscard]] std::vector<Chunk> planChunks (const float* filtered,
                                                 int numFilteredSamples,
                                                 int numPaddedSamples,
                                                 int numFrames) const;

    /** Runs the content encoder over one chunk, returning `[numFrames, featureDim]`. */
    [[nodiscard]] std::vector<float> encodeContent (const float* samples,
                                                   int numSamples,
                                                   juce::String& error) const;

    /** Runs the vocoder over one chunk, returning audio at the model's sample rate. */
    [[nodiscard]] std::vector<float> synthesise (const std::vector<float>& contentFeatures,
                                                const std::int64_t* coarsePitch,
                                                const float* fundamentalFrequencyHz,
                                                int numFrames,
                                                const ConversionSettings& settings,
                                                juce::String& error) const;

    /** Scales the conversion toward the source's loudness contour. */
    void followSourceEnvelope (std::vector<float>& converted,
                               const float* source,
                               int numSourceSamples,
                               double sourceSampleRate,
                               float ratio) const;

    const VoiceModel& voiceModel;
    const ModelManifest& manifest;
};

} // namespace rvcara
