#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rvcara
{
/** @brief Converts a frequency to RVC's mel scale; zero and negative frequencies mean unvoiced.
    @param frequencyHz  Frequency in hertz.
    @return Position on the mel scale, or zero when unvoiced.
*/
inline double hzToMel (double frequencyHz) noexcept
{
    if (frequencyHz <= 0.0)
        return 0.0;

    return 1127.0 * std::log (1.0 + frequencyHz / 700.0);
}

/** @brief Inverse of hzToMel().
    @param mel  Position on the mel scale.
    @return Frequency in hertz.
*/
inline double melToHz (double mel) noexcept
{
    return 700.0 * (std::exp (mel / 1127.0) - 1.0);
}

/** @brief Expresses a frequency as an interval in cents above a reference.
    @param frequencyHz  Frequency in hertz.
    @param referenceHz  Reference frequency in hertz.
    @return Interval in cents, or zero when either frequency is unvoiced.
*/
inline double hzToCents (double frequencyHz, double referenceHz) noexcept
{
    if (frequencyHz <= 0.0 || referenceHz <= 0.0)
        return 0.0;

    return 1200.0 * std::log2 (frequencyHz / referenceHz);
}

/** @brief Inverse of hzToCents().
    @param cents        Interval in cents.
    @param referenceHz  Reference frequency in hertz.
    @return Frequency in hertz.
*/
inline double centsToHz (double cents, double referenceHz) noexcept
{
    return referenceHz * std::exp2 (cents / 1200.0);
}

/** @brief Converts a transposition in semitones to a frequency ratio.
    @param semitones  Interval in semitones.
    @return Ratio to multiply a frequency by.
*/
inline double semitonesToRatio (double semitones) noexcept
{
    return std::exp2 (semitones / 12.0);
}

/** @brief Expresses a frequency as a MIDI note number.
    @param frequencyHz  Frequency in hertz.
    @return The note number, where 69 is A440, or zero when unvoiced.
*/
inline double hzToMidiNote (double frequencyHz) noexcept
{
    if (frequencyHz <= 0.0)
        return 0.0;

    return 69.0 + 12.0 * std::log2 (frequencyHz / 440.0);
}

/** @brief Inverse of hzToMidiNote().
    @param midiNote  The note number, where 69 is A440.
    @return Frequency in hertz.
*/
inline double midiNoteToHz (double midiNote) noexcept
{
    return 440.0 * std::exp2 ((midiNote - 69.0) / 12.0);
}

/** @brief Maps a frequency onto the vocoder's coarse pitch embedding, mel spaced. */
class CoarsePitchQuantiser
{
public:
    CoarsePitchQuantiser (double minimumFrequencyHz, double maximumFrequencyHz, int numBins) noexcept
        : melMinimum (hzToMel (minimumFrequencyHz)),
          melMaximum (hzToMel (maximumFrequencyHz)),
          numberOfBins (numBins)
    {
    }

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

    [[nodiscard]] int getNumBins() const noexcept { return numberOfBins; }

private:
    double melMinimum;
    double melMaximum;
    int numberOfBins;
};
}
