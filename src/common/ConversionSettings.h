#pragma once

#include <bit>
#include <cstdint>

namespace rvcara
{
/** @brief Everything about a conversion the user can change, and the hash a cache keys on. */
struct ConversionSettings
{
    float pitchShiftSemitones { 0.0f };

    float retrievalRatio { 0.75f };

    float consonantProtection { 0.33f };

    float envelopeFollowRatio { 0.0f };

    std::int32_t latentNoiseSeed { 1 };

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

    [[nodiscard]] std::uint64_t getHash() const noexcept
    {
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
}
