#include "common/ConversionSettings.h"

#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <vector>

using namespace rvcara;

namespace
{
    using Change = std::function<void (ConversionSettings&)>;

    std::vector<Change> everyField()
    {
        return { [] (auto& settings) { settings.pitchShiftSemitones = 1.0f; },
                 [] (auto& settings) { settings.retrievalRatio = 0.5f; },
                 [] (auto& settings) { settings.consonantProtection = 0.1f; },
                 [] (auto& settings) { settings.envelopeFollowRatio = 0.5f; },
                 [] (auto& settings) { settings.latentNoiseSeed = 2; },
                 [] (auto& settings) { settings.isBypassed = true; } };
    }
} // namespace

TEST (ConversionSettings, DefaultsMatchTheReferenceConfiguration)
{
    const ConversionSettings settings;

    EXPECT_EQ (settings.pitchShiftSemitones, 0.0f);
    EXPECT_EQ (settings.retrievalRatio, 0.75f);
    EXPECT_EQ (settings.consonantProtection, 0.33f);
    EXPECT_EQ (settings.envelopeFollowRatio, 0.0f);
    EXPECT_EQ (settings.latentNoiseSeed, 1);
    EXPECT_FALSE (settings.isBypassed);
}

TEST (ConversionSettings, EqualityComparesEveryField)
{
    const ConversionSettings reference;
    EXPECT_TRUE (reference == ConversionSettings {});

    for (const auto& change : everyField())
    {
        auto changed = reference;
        change (changed);
        EXPECT_TRUE (changed != reference);
    }
}

TEST (ConversionSettings, TheHashDistinguishesEveryField)
{
    const ConversionSettings reference;
    std::set<std::uint64_t> hashes { reference.getHash() };

    for (const auto& change : everyField())
    {
        auto changed = reference;
        change (changed);
        EXPECT_TRUE (hashes.insert (changed.getHash()).second);
    }
}

TEST (ConversionSettings, TheHashIsStableAndAgreesWithEquality)
{
    ConversionSettings first;
    first.pitchShiftSemitones = -3.5f;
    first.retrievalRatio = 0.42f;

    const auto second = first;

    EXPECT_TRUE (first == second);
    EXPECT_EQ (first.getHash(), second.getHash());
}

TEST (ConversionSettings, AFractionalPitchShiftIsDistinguishableFromZero)
{
    ConversionSettings nudged;
    nudged.pitchShiftSemitones = 0.01f;

    EXPECT_NE (nudged.getHash(), ConversionSettings {}.getHash());
}
