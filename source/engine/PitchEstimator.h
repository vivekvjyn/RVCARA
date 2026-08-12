#pragma once

#include "../dsp/MelSpectrogram.h"
#include "../dsp/PitchConversions.h"
#include "ModelManifest.h"
#include "OnnxSession.h"

#include <vector>

namespace rvcara::engine
{

/** Estimates the melody of a performance, in the two forms the vocoder needs.

    Wraps the RMVPE network with everything around it: the spectrogram front end, the
    padding the network's five-stage U-Net requires, the salience decode, the gap fill,
    the transposition, and the mel quantiser that indexes the vocoder's pitch
    embedding. The network itself is a single ONNX call in the middle.

    The result is two parallel arrays, both at the conditioning rate:

    - @c fundamentalFrequencyHz, continuous pitch, which drives the vocoder's harmonic
      excitation and therefore determines what note comes out;
    - @c coarsePitchBins, integer indices into the pitch embedding, which tell the
      vocoder's text encoder roughly where in its range it is singing.

    Both are needed and they are not redundant: the embedding conditions timbre, the
    continuous track conditions the excitation.
*/
class PitchEstimator
{
public:
    /** The estimated melody. */
    struct Result
    {
        std::vector<float> fundamentalFrequencyHz;  ///< Continuous pitch, one per frame
        std::vector<std::int64_t> coarsePitchBins;  ///< Embedding indices in [1, 255]

        [[nodiscard]] int getNumFrames() const noexcept
        {
            return static_cast<int> (fundamentalFrequencyHz.size());
        }
    };

    /** @param manifest      Supplies every front-end and decoder constant.
        @param spectrogram   The shared front end; must outlive this object.
        @param network       The loaded pitch estimator graph; must outlive this object.
    */
    PitchEstimator (const ModelManifest& manifest,
                    const dsp::MelSpectrogram& spectrogram,
                    const OnnxSession& network);

    /** Estimates pitch over a whole signal.

        @param samples               Mono audio at the front end's sample rate.
        @param numSamples            Its length.
        @param pitchShiftSemitones   Transposition applied after estimation.
        @param error                 Set if the network call fails.
        @returns                     The melody, empty on failure.
    */
    [[nodiscard]] Result estimate (const float* samples,
                                   int numSamples,
                                   float pitchShiftSemitones,
                                   juce::String& error) const;

private:
    /** Turns one frame's salience into a frequency.

        The peak class gives a coarse pitch; the salience-weighted mean of the classes
        within @c localAverageRadius of it refines that to well under the 20-cent class
        spacing, which is what makes the estimator usable on vibrato. A frame whose peak
        salience is below threshold is reported as zero, meaning unvoiced.

        @param salienceFrame  One frame's @c numPitchBins activations.
        @returns              Frequency in hertz, or zero if unvoiced.
    */
    [[nodiscard]] double decodeFrame (const float* salienceFrame) const;

    /** Fills unvoiced frames by interpolating between the voiced ones either side.

        The reference pipeline does this so the harmonic excitation stays phase-continuous
        through consonants rather than restarting, which would click. It has a
        consequence worth stating plainly: afterwards nothing is zero, so the
        consonantProtection blend in ConversionEngine has nothing left to act on. That
        is upstream's behaviour, and it is reproduced rather than silently corrected,
        because the whole point of this engine is to render what the trained model was
        validated against.

        @param frequencies  Modified in place; left alone if nothing or everything is voiced.
    */
    static void fillUnvoicedGaps (std::vector<float>& frequencies);

    const ModelManifest& manifest;
    const dsp::MelSpectrogram& melSpectrogram;
    const OnnxSession& pitchNetwork;
    dsp::CoarsePitchQuantiser quantiser;

    std::vector<double> centsPerClass;  // precomputed class centre for each pitch bin
};

} // namespace rvcara::engine
