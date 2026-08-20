#pragma once

#include "common/PitchEdit.h"
#include "model/OnnxSession.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace rvcara
{
/** @brief GAME note segmentation: a sung take in, the notes it is made of out.

    Four graphs run in turn. An encoder embeds the waveform, a diffusion segmenter proposes the
    boundaries between notes, another turns those boundaries into durations, and an estimator
    names each note's pitch and says whether anything was sung in it. Long takes are cut at
    silences first, because the encoder only accepts a bounded number of frames.
*/
class NoteSegmenter
{
public:
    /** @brief The directory the graphs are installed in, beside the voices. */
    static constexpr const char* modelDirectoryName = "GAME";

    /** @brief Returns the one segmenter this process shares, loading it on first use.

        The graphs are read only and about fifty megabytes, so every editor borrows the same
        copy instead of loading one each.

        @param error  Set when the graphs could not be loaded.
        @return The shared segmenter, or nullptr when it could not be loaded.
    */
    [[nodiscard]] static std::shared_ptr<const NoteSegmenter> getShared (juce::String& error);

    /** @brief Where the graphs are expected to be installed. */
    [[nodiscard]] static juce::File getModelDirectory();

    /** @brief Splits a performance into the notes it is made of.
        @param samples      The take, one channel.
        @param numSamples   How many samples it has.
        @param sampleRate   The rate those samples are at.
        @param shouldAbort  Polled between chunks, so a cancelled edit stops promptly.
        @param error        Set when segmentation failed.
        @return The notes in time order, with rests where nothing was sung.
    */
    [[nodiscard]] std::vector<EditedNote> segment (const float* samples,
                                                   int numSamples,
                                                   double sampleRate,
                                                   const std::atomic<bool>& shouldAbort,
                                                   juce::String& error) const;

private:
    NoteSegmenter() = default;

    /** @brief One stretch of the take handed to the encoder whole. */
    struct Chunk
    {
        int startSample { 0 };
        int endSample { 0 };
    };

    [[nodiscard]] bool load (const juce::File& directory, juce::String& error);

    [[nodiscard]] std::vector<Chunk> planChunks (const std::vector<float>& waveform) const;

    [[nodiscard]] std::vector<EditedNote> segmentChunk (const float* samples,
                                                        int numSamples,
                                                        double startSeconds,
                                                        juce::String& error) const;

    [[nodiscard]] int getSamplesPerFrame() const noexcept
    {
        return static_cast<int> (static_cast<double> (modelSampleRate) * timestep);
    }

    OnnxSession encoder;
    OnnxSession boundaryFinder;
    OnnxSession durationMaker;
    OnnxSession pitchNamer;

    std::vector<std::string> encoderInputs;
    std::vector<std::string> boundaryFinderInputs;
    std::vector<std::string> durationMakerInputs;
    std::vector<std::string> pitchNamerInputs;

    int modelSampleRate { 44100 };
    double timestep { 0.01 };
    bool isDiffusion { true };
};
}
