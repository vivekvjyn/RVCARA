#pragma once

#include <bit>
#include <cstdint>

namespace rvcara::engine
{

/** The controls a user turns, and the complete input to a conversion besides the audio.

    Deliberately a plain aggregate with value semantics. It is compared for equality to
    decide whether a cached conversion is still valid, hashed to key that cache, and
    copied across thread boundaries on every parameter change — all of which want a
    trivially copyable type with no allocation and no indirection.

    Adding a field here means adding it to operator== and to getHash(), or a cached
    render will survive a change that should have invalidated it.
*/
struct ConversionSettings
{
    /** Transposition applied to the estimated pitch, in semitones.

        Applied after estimation rather than before, so it shifts the pitch the singer
        actually sang. Fractional values are meaningful.
    */
    float pitchShiftSemitones { 0.0f };

    /** How far to move content features toward the retrieved timbre, in [0, 1].

        At zero the voice is the source performer's, filtered through the model's
        vocoder. At one it is the trained singer's timbre, at some cost in
        intelligibility when the source phonemes are not represented in the training
        set. The trained default is 0.75.
    */
    float retrievalRatio { 0.75f };

    /** Keeps unvoiced frames nearer the source features, in [0, 0.5).

        Intended to stop retrieval from smearing consonants. Values of 0.5 and above
        disable it. See the note in ConversionEngine on why this is inert for models
        whose pitch track is gap-filled.
    */
    float consonantProtection { 0.33f };

    /** How much of the source's loudness contour to restore, in [0, 1].

        Zero leaves the conversion's own dynamics alone. Raising it reimposes the
        source performance's envelope, which helps when the model's dynamic range
        differs from the singer's.
    */
    float envelopeFollowRatio { 0.0f };

    /** Seed for the vocoder's latent sample.

        The vocoder's excitation is stochastic; fixing the seed makes a render
        reproducible, which ARA requires — a host may re-render the same region many
        times and must get the same samples back. Changing it gives a different valid
        performance of the same line, which is why it is exposed rather than hidden.
    */
    std::int32_t latentNoiseSeed { 1 };

    /** Whether this modification is bypassed, passing the source audio through. */
    bool isBypassed { false };

    [[nodiscard]] bool operator== (const ConversionSettings& other) const noexcept
    {
        return pitchShiftSemitones == other.pitchShiftSemitones
            && retrievalRatio == other.retrievalRatio
            && consonantProtection == other.consonantProtection
            && envelopeFollowRatio == other.envelopeFollowRatio
            && latentNoiseSeed == other.latentNoiseSeed
            && isBypassed == other.isBypassed;
    }

    [[nodiscard]] bool operator!= (const ConversionSettings& other) const noexcept
    {
        return ! (*this == other);
    }

    /** @returns A hash identifying these settings, for cache keys and archive checks. */
    [[nodiscard]] std::uint64_t getHash() const noexcept
    {
        // FNV-1a over the float bit patterns. Bit patterns rather than values so that
        // the hash agrees with operator==, which also compares exactly.
        std::uint64_t hash = 0xcbf29ce484222325ULL;

        const auto mix = [&hash] (std::uint32_t word)
        {
            for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
            {
                hash ^= (word >> (8 * byteIndex)) & 0xffU;
                hash *= 0x100000001b3ULL;
            }
        };

        mix (std::bit_cast<std::uint32_t> (pitchShiftSemitones));
        mix (std::bit_cast<std::uint32_t> (retrievalRatio));
        mix (std::bit_cast<std::uint32_t> (consonantProtection));
        mix (std::bit_cast<std::uint32_t> (envelopeFollowRatio));
        mix (static_cast<std::uint32_t> (latentNoiseSeed));
        mix (isBypassed ? 1U : 0U);

        return hash;
    }
};

} // namespace rvcara::engine
