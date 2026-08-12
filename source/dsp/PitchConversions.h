#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

/** Conversions between the four pitch representations the pipeline uses.

    A single frame of pitch is expressed four different ways between the estimator
    and the vocoder, and confusing two of them is the easiest way to produce audio
    that is recognisably a voice singing recognisably the wrong notes:

    - **hertz**, the physical frequency, what the pitch estimator ultimately reports
      and what the vocoder's harmonic excitation is driven by;
    - **cents**, a logarithmic interval above a 10 Hz reference, which is how the
      estimator's 360 output classes are spaced and the only unit in which a pitch
      error is perceptually meaningful;
    - **mel**, a perceptual frequency scale, used solely as the spacing for the
      vocoder's pitch embedding table;
    - **coarse bins**, integers from 1 to 255 indexing that table.

    Everything here is `constexpr`-friendly, free of state, and defined for the
    degenerate inputs the pipeline actually produces — in particular zero hertz,
    which means "unvoiced" rather than "0 Hz" and must not become a logarithm of
    zero.
*/
namespace rvcara::dsp
{

/** The mel scale as RVC defines it.

    Note this is the 1127-and-700 form of the mel scale, not the 2595-and-700 form
    used for log10, and not the HTK variant the pitch estimator's filter bank uses.
    The two appear within a few hundred lines of each other in this codebase and are
    not interchangeable: this one spaces the vocoder's pitch embedding, the other
    spaces the spectrogram's mel bins.

    @param frequencyHz  Frequency in hertz; zero and negative inputs map to zero.
    @returns            Position on the mel scale.
*/
inline double hzToMel (double frequencyHz) noexcept
{
    if (frequencyHz <= 0.0)
        return 0.0;

    return 1127.0 * std::log (1.0 + frequencyHz / 700.0);
}

/** Inverse of hzToMel().

    @param mel  Position on the mel scale.
    @returns    Frequency in hertz.
*/
inline double melToHz (double mel) noexcept
{
    return 700.0 * (std::exp (mel / 1127.0) - 1.0);
}

/** Converts a frequency to cents above a reference.

    @param frequencyHz  Frequency in hertz; zero and negative inputs map to zero.
    @param referenceHz  The frequency that sits at zero cents.
    @returns            Interval in cents, 100 to the semitone.
*/
inline double hzToCents (double frequencyHz, double referenceHz) noexcept
{
    if (frequencyHz <= 0.0 || referenceHz <= 0.0)
        return 0.0;

    return 1200.0 * std::log2 (frequencyHz / referenceHz);
}

/** Inverse of hzToCents().

    @param cents        Interval in cents.
    @param referenceHz  The frequency that sits at zero cents.
    @returns            Frequency in hertz.
*/
inline double centsToHz (double cents, double referenceHz) noexcept
{
    return referenceHz * std::exp2 (cents / 1200.0);
}

/** Converts a transposition in semitones to the ratio that applies it.

    @param semitones  Transposition; positive raises the pitch.
    @returns          Multiplier for a frequency.
*/
inline double semitonesToRatio (double semitones) noexcept
{
    return std::exp2 (semitones / 12.0);
}

/** The mel-spaced quantiser that indexes the vocoder's pitch embedding.

    Constructed from the manifest so a model trained over a different pitch range
    stays loadable. The mapping places @c minimumFrequencyHz at bin 1 and
    @c maximumFrequencyHz at bin @c numBins, spaced by mel rather than by log
    frequency, so a semitone near the bottom of the range spans more bins than one
    near the top.
*/
class CoarsePitchQuantiser
{
public:
    /** @param minimumFrequencyHz  Frequency mapping to the first bin.
        @param maximumFrequencyHz  Frequency mapping to the last bin.
        @param numBins             Number of bins, 255 for every RVC model so far.
    */
    CoarsePitchQuantiser (double minimumFrequencyHz, double maximumFrequencyHz, int numBins) noexcept
        : melMinimum (hzToMel (minimumFrequencyHz)),
          melMaximum (hzToMel (maximumFrequencyHz)),
          numberOfBins (numBins)
    {
    }

    /** Quantises one frame.

        Unvoiced frames — zero hertz — land on bin 1 rather than bin 0, because the
        embedding table has no entry for "no pitch" and the vocoder's excitation is
        gated by the continuous pitch instead.

        @param frequencyHz  Pitch in hertz, or zero for unvoiced.
        @returns            Bin index in [1, numBins].
    */
    [[nodiscard]] std::int64_t toBin (double frequencyHz) const noexcept
    {
        const auto mel = hzToMel (frequencyHz);

        if (mel <= 0.0)
            return 1;

        const auto span = melMaximum - melMinimum;
        const auto scaled = (mel - melMinimum) * static_cast<double> (numberOfBins - 1) / span + 1.0;

        return static_cast<std::int64_t> (
            std::clamp (std::nearbyint (scaled), 1.0, static_cast<double> (numberOfBins)));
    }

    /** @returns The number of bins the quantiser spans. */
    [[nodiscard]] int getNumBins() const noexcept { return numberOfBins; }

private:
    double melMinimum;
    double melMaximum;
    int numberOfBins;
};

} // namespace rvcara::dsp
