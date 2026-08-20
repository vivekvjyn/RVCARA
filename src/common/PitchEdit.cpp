#include "common/PitchEdit.h"

#include "dsp/PitchConversions.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace rvcara
{
namespace
{
    /** @brief Returns the half-open range of frames a stretch of time covers. */
    std::pair<int, int> getFrameRange (int numFrames,
                                       double frameRate,
                                       int firstFrame,
                                       double startSeconds,
                                       double endSeconds)
    {
        const auto toFrame = [&] (double seconds)
        {
            const auto frame = static_cast<double> (firstFrame) + seconds * frameRate;
            return static_cast<int> (std::clamp (std::llround (frame),
                                                 static_cast<long long> (0),
                                                 static_cast<long long> (numFrames)));
        };

        const auto first = toFrame (startSeconds);
        return { first, std::max (first, toFrame (endSeconds)) };
    }
} // namespace

bool PitchEdit::isNeutral() const noexcept
{
    return std::all_of (notes.begin(), notes.end(),
                        [] (const EditedNote& note) { return note.isNeutral(); });
}

std::uint64_t PitchEdit::getHash() const noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;

    const auto mix = [&hash] (std::uint64_t word)
    {
        for (int byteIndex = 0; byteIndex < 8; ++byteIndex)
        {
            hash ^= (word >> (8 * byteIndex)) & 0xffU;
            hash *= 0x100000001b3ULL;
        }
    };

    for (const auto& note : notes)
    {
        mix (std::bit_cast<std::uint64_t> (note.startSeconds));
        mix (std::bit_cast<std::uint64_t> (note.endSeconds));
        mix (std::bit_cast<std::uint32_t> (note.sungMidiNote));
        mix (std::bit_cast<std::uint32_t> (note.offsetSemitones));
        mix (std::bit_cast<std::uint32_t> (note.depth));
        mix (std::bit_cast<std::uint32_t> (note.tiltLeft));
        mix (std::bit_cast<std::uint32_t> (note.tiltRight));
        mix (note.isRest ? 1U : 0U);
    }

    return hash;
}

float getSungMidiNote (const std::vector<float>& fundamentalFrequencyHz,
                       double frameRate,
                       int firstFrame,
                       double startSeconds,
                       double endSeconds)
{
    if (frameRate <= 0.0)
        return 0.0f;

    const auto numFrames = static_cast<int> (fundamentalFrequencyHz.size());
    const auto [first, last] = getFrameRange (numFrames, frameRate, firstFrame, startSeconds, endSeconds);

    std::vector<float> notes;
    notes.reserve (static_cast<std::size_t> (last - first));

    for (int frameIndex = first; frameIndex < last; ++frameIndex)
    {
        const auto frequencyHz = fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)];

        if (frequencyHz > 0.0f)
            notes.push_back (static_cast<float> (hzToMidiNote (static_cast<double> (frequencyHz))));
    }

    if (notes.empty())
        return 0.0f;

    const auto middle = notes.begin() + static_cast<std::ptrdiff_t> (notes.size() / 2);
    std::nth_element (notes.begin(), middle, notes.end());

    return *middle;
}

void applyPitchEdit (std::vector<float>& fundamentalFrequencyHz,
                     const PitchEdit& edit,
                     double frameRate,
                     int firstFrame)
{
    if (frameRate <= 0.0 || edit.isNeutral())
        return;

    const auto numFrames = static_cast<int> (fundamentalFrequencyHz.size());

    // Centres come from the untouched track, so one note's move cannot drag a neighbour that
    // happens to overlap it.
    const auto sung = fundamentalFrequencyHz;

    for (const auto& note : edit.notes)
    {
        if (note.isNeutral())
            continue;

        const auto centre = getSungMidiNote (sung, frameRate, firstFrame,
                                             note.startSeconds, note.endSeconds);

        if (centre <= 0.0f)
            continue;

        const auto [first, last] = getFrameRange (numFrames, frameRate, firstFrame,
                                                  note.startSeconds, note.endSeconds);

        const auto span = static_cast<float> (last - first);

        for (int frameIndex = first; frameIndex < last; ++frameIndex)
        {
            auto& frequencyHz = fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)];

            if (frequencyHz <= 0.0f)
                continue;

            const auto position = span > 1.0f ? static_cast<float> (frameIndex - first) / (span - 1.0f)
                                              : 0.0f;

            const auto tilt = position < 0.5f ? note.tiltLeft * (1.0f - 2.0f * position)
                                              : note.tiltRight * (2.0f * position - 1.0f);

            const auto midiNote = static_cast<float> (hzToMidiNote (static_cast<double> (frequencyHz)));
            const auto shaped = centre + note.offsetSemitones
                              + (midiNote - centre) * note.depth + tilt;

            frequencyHz = static_cast<float> (midiNoteToHz (static_cast<double> (shaped)));
        }
    }
}
}
