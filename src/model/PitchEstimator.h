#pragma once

#include "dsp/MelSpectrogram.h"
#include "dsp/PitchConversions.h"
#include "model/ModelManifest.h"
#include "model/OnnxSession.h"

#include <vector>

namespace rvcara
{
/** @brief RMVPE pitch estimation: log-mel in, a gap-filled fundamental and its coarse bins out. */
class PitchEstimator
{
public:
    /** @brief The estimated melody, as a continuous track and as coarse bins. */
    struct Result
    {
        std::vector<float> fundamentalFrequencyHz;
        std::vector<std::int64_t> coarsePitchBins;

        [[nodiscard]] int getNumFrames() const noexcept
        {
            return static_cast<int> (fundamentalFrequencyHz.size());
        }
    };

    PitchEstimator (const ModelManifest& manifest,
                    const MelSpectrogram& spectrogram,
                    const OnnxSession& network);

    [[nodiscard]] Result estimate (const float* samples,
                                   int numSamples,
                                   float pitchShiftSemitones,
                                   juce::String& error) const;

private:
    [[nodiscard]] double decodeFrame (const float* salienceFrame) const;

    static void fillUnvoicedGaps (std::vector<float>& frequencies);

    const ModelManifest& manifest;
    const MelSpectrogram& melSpectrogram;
    const OnnxSession& pitchNetwork;
    CoarsePitchQuantiser quantiser;

    std::vector<double> centsPerClass;
};
}
