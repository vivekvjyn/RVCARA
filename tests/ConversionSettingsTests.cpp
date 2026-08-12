#include <engine/ConversionSettings.h>

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace rvcara::engine;

TEST_CASE ("default settings match the reference configuration", "[settings]")
{
    const ConversionSettings settings;

    CHECK (settings.pitchShiftSemitones == 0.0f);
    CHECK (settings.retrievalRatio == 0.75f);
    CHECK (settings.consonantProtection == 0.33f);
    CHECK (settings.envelopeFollowRatio == 0.0f);
    CHECK (settings.latentNoiseSeed == 1);
    CHECK_FALSE (settings.isBypassed);
}

TEST_CASE ("equality compares every field", "[settings]")
{
    // A cached render is reused when the settings compare equal, so a field left out of
    // operator== means a control the user turns has no audible effect.
    const ConversionSettings reference;

    CHECK (reference == ConversionSettings {});

    {
        auto changed = reference;
        changed.pitchShiftSemitones = 1.0f;
        CHECK (changed != reference);
    }
    {
        auto changed = reference;
        changed.retrievalRatio = 0.5f;
        CHECK (changed != reference);
    }
    {
        auto changed = reference;
        changed.consonantProtection = 0.1f;
        CHECK (changed != reference);
    }
    {
        auto changed = reference;
        changed.envelopeFollowRatio = 0.5f;
        CHECK (changed != reference);
    }
    {
        auto changed = reference;
        changed.latentNoiseSeed = 2;
        CHECK (changed != reference);
    }
    {
        auto changed = reference;
        changed.isBypassed = true;
        CHECK (changed != reference);
    }
}

TEST_CASE ("the hash distinguishes every field", "[settings]")
{
    // The same requirement as equality, in the form the cache actually keys on. A field
    // missing here produces a stale render that survives a parameter change.
    const ConversionSettings reference;
    std::set<std::uint64_t> hashes { reference.getHash() };

    const auto record = [&hashes] (ConversionSettings settings)
    {
        const auto inserted = hashes.insert (settings.getHash()).second;
        CHECK (inserted);
    };

    {
        auto changed = reference;
        changed.pitchShiftSemitones = 1.0f;
        record (changed);
    }
    {
        auto changed = reference;
        changed.retrievalRatio = 0.5f;
        record (changed);
    }
    {
        auto changed = reference;
        changed.consonantProtection = 0.1f;
        record (changed);
    }
    {
        auto changed = reference;
        changed.envelopeFollowRatio = 0.5f;
        record (changed);
    }
    {
        auto changed = reference;
        changed.latentNoiseSeed = 2;
        record (changed);
    }
    {
        auto changed = reference;
        changed.isBypassed = true;
        record (changed);
    }
}

TEST_CASE ("the hash is stable and agrees with equality", "[settings]")
{
    ConversionSettings first;
    first.pitchShiftSemitones = -3.5f;
    first.retrievalRatio = 0.42f;

    auto second = first;

    CHECK (first == second);
    CHECK (first.getHash() == second.getHash());
    CHECK (first.getHash() == first.getHash());
}

TEST_CASE ("a fractional pitch shift is distinguishable from zero", "[settings]")
{
    // Bit-pattern hashing rather than a rounded one, so a quarter-tone nudge invalidates
    // the cache like any other change.
    ConversionSettings nudged;
    nudged.pitchShiftSemitones = 0.01f;

    CHECK (nudged.getHash() != ConversionSettings {}.getHash());
}
