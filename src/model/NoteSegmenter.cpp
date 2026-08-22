#include "model/NoteSegmenter.h"

#include "dsp/SincResampler.h"
#include "model/VoiceModelLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace rvcara
{
namespace
{
    /** @brief How many refinement passes the diffusion segmenter makes over its boundaries. */
    constexpr int numDiffusionSteps = 2;

    constexpr float boundaryThreshold = 0.2f;
    constexpr std::int64_t boundaryRadius = 2;
    constexpr float presenceThreshold = 0.2f;

    /** @brief The encoder's frame limit, which is what forces a long take to be cut up. */
    constexpr int maximumEncoderFrames = 5000;

    /** @brief Quieter than this counts as silence, which is where the take is cut. */
    constexpr float silenceThreshold = 0.01f;

    constexpr int minimumChunkMilliseconds = 1000;
    constexpr int minimumSilenceMilliseconds = 200;
    constexpr int keptSilenceMilliseconds = 100;

    const std::uint8_t* asBytes (const bool* values) noexcept
    {
        return reinterpret_cast<const std::uint8_t*> (values);
    }
} // namespace

juce::File NoteSegmenter::getModelDirectory()
{
    return VoiceModelLibrary::findAssetDirectory (modelDirectoryName);
}

std::shared_ptr<const NoteSegmenter> NoteSegmenter::getShared (juce::String& error)
{
    static juce::CriticalSection loadLock;
    static std::weak_ptr<const NoteSegmenter> shared;

    const juce::ScopedLock lock { loadLock };

    if (auto existing = shared.lock())
        return existing;

    const std::shared_ptr<NoteSegmenter> segmenter { new NoteSegmenter };

    if (! segmenter->load (getModelDirectory(), error))
        return nullptr;

    shared = segmenter;
    return segmenter;
}

bool NoteSegmenter::load (const juce::File& directory, juce::String& error)
{
    if (! directory.isDirectory())
    {
        error = "no note model installed in " + directory.getFullPathName();
        return false;
    }

    const auto configuration = juce::JSON::parse (directory.getChildFile ("config.json").loadFileAsString());

    if (auto* root = configuration.getDynamicObject())
    {
        modelSampleRate = static_cast<int> (static_cast<double> (root->getProperty ("samplerate")));
        timestep = static_cast<double> (root->getProperty ("timestep"));
        isDiffusion = static_cast<bool> (root->getProperty ("loop"));
    }

    if (modelSampleRate <= 0 || timestep <= 0.0)
    {
        error = "the note model's config.json does not say what rate or timestep it wants";
        return false;
    }

    struct Graph
    {
        OnnxSession& session;
        std::vector<std::string>& inputNames;
        const char* fileName;
    };

    const Graph graphs[] {
        { encoder, encoderInputs, "encoder.onnx" },
        { boundaryFinder, boundaryFinderInputs, "segmenter.onnx" },
        { durationMaker, durationMakerInputs, "bd2dur.onnx" },
        { pitchNamer, pitchNamerInputs, "estimator.onnx" },
    };

    // Detection runs beside a conversion, so leave that half of the machine to it.
    const auto numThreads = std::max (juce::SystemStats::getNumPhysicalCpus() / 2, 1);

    for (const auto& graph : graphs)
    {
        if (! graph.session.load (directory.getChildFile (graph.fileName), numThreads))
        {
            error = graph.session.getError();
            return false;
        }

        graph.inputNames = graph.session.getInputNames();
    }

    return true;
}

std::vector<NoteSegmenter::Chunk> NoteSegmenter::planChunks (const std::vector<float>& waveform) const
{
    const auto numSamples = static_cast<int> (waveform.size());

    if (numSamples <= 0)
        return {};

    const auto hopSize = getSamplesPerFrame();
    const auto numHops = (numSamples + hopSize - 1) / hopSize;

    const auto toHops = [this, hopSize] (int milliseconds)
    {
        return milliseconds * modelSampleRate / (1000 * hopSize);
    };

    const auto minimumChunkHops = toHops (minimumChunkMilliseconds);
    const auto minimumSilenceHops = toHops (minimumSilenceMilliseconds);
    const auto keptSilenceHops = toHops (keptSilenceMilliseconds);

    std::vector<float> loudness (static_cast<std::size_t> (numHops), 0.0f);

    for (int hopIndex = 0; hopIndex < numHops; ++hopIndex)
    {
        const auto first = hopIndex * hopSize;
        const auto last = std::min (first + hopSize, numSamples);

        double sumOfSquares = 0.0;

        for (int sampleIndex = first; sampleIndex < last; ++sampleIndex)
            sumOfSquares += static_cast<double> (waveform[static_cast<std::size_t> (sampleIndex)])
                          * static_cast<double> (waveform[static_cast<std::size_t> (sampleIndex)]);

        loudness[static_cast<std::size_t> (hopIndex)] =
            static_cast<float> (std::sqrt (sumOfSquares / static_cast<double> (std::max (last - first, 1))));
    }

    /** @brief A run of hops with singing in it, before it is padded back out to samples. */
    struct HopRange
    {
        int firstHop { 0 };
        int lastHop { 0 };
    };

    std::vector<HopRange> voiced;
    {
        auto silenceStart = -1;
        auto voicedStart = 0;

        for (int hopIndex = 0; hopIndex <= numHops; ++hopIndex)
        {
            const auto isSilent = hopIndex < numHops
                               && loudness[static_cast<std::size_t> (hopIndex)] < silenceThreshold;

            if (isSilent && silenceStart < 0)
                silenceStart = hopIndex;

            if (! isSilent && silenceStart >= 0)
            {
                if (hopIndex - silenceStart >= minimumSilenceHops)
                {
                    if (silenceStart > voicedStart)
                        voiced.push_back ({ voicedStart, silenceStart });

                    voicedStart = hopIndex;
                }

                silenceStart = -1;
            }
        }

        if (voicedStart < numHops)
            voiced.push_back ({ voicedStart, numHops });
    }

    if (voiced.empty())
        return { Chunk { 0, numSamples } };

    for (std::size_t index = 0; index + 1 < voiced.size();)
    {
        if (voiced[index].lastHop - voiced[index].firstHop < minimumChunkHops)
        {
            voiced[index].lastHop = voiced[index + 1].lastHop;
            voiced.erase (voiced.begin() + static_cast<std::ptrdiff_t> (index) + 1);
        }
        else
        {
            ++index;
        }
    }

    if (voiced.size() > 1
        && voiced.back().lastHop - voiced.back().firstHop < minimumChunkHops)
    {
        voiced[voiced.size() - 2].lastHop = voiced.back().lastHop;
        voiced.pop_back();
    }

    const auto maximumChunkSamples = maximumEncoderFrames * hopSize;

    std::vector<Chunk> chunks;

    for (const auto& segment : voiced)
    {
        auto start = std::max (segment.firstHop - keptSilenceHops, 0) * hopSize;
        const auto end = std::min (numSamples, (segment.lastHop + keptSilenceHops) * hopSize);

        while (end - start > maximumChunkSamples)
        {
            const auto searchFirst = (start + maximumChunkSamples / 2) / hopSize;
            const auto searchLast = std::min ((start + maximumChunkSamples) / hopSize, numHops);

            auto quietestHop = -1;
            auto quietest = std::numeric_limits<float>::max();

            for (int hopIndex = searchFirst; hopIndex < searchLast; ++hopIndex)
            {
                if (loudness[static_cast<std::size_t> (hopIndex)] < quietest)
                {
                    quietest = loudness[static_cast<std::size_t> (hopIndex)];
                    quietestHop = hopIndex;
                }
            }

            const auto split = std::min (quietestHop >= 0 ? quietestHop * hopSize
                                                          : start + maximumChunkSamples,
                                         end);

            if (split <= start)
                break;

            chunks.push_back ({ start, split });
            start = split;
        }

        if (end > start)
            chunks.push_back ({ start, end });
    }

    return chunks;
}

std::vector<EditedNote> NoteSegmenter::segmentChunk (const float* samples,
                                                     int numSamples,
                                                     double startSeconds,
                                                     juce::String& error) const
{
    const auto durationSeconds = static_cast<float> (numSamples) / static_cast<float> (modelSampleRate);

    std::vector<OnnxSession::TensorView> encoderArguments;

    for (const auto& name : encoderInputs)
    {
        if (name == "waveform")
            encoderArguments.push_back (OnnxSession::TensorView::floats (name.c_str(), samples, { 1, numSamples }));
        else if (name == "duration")
            encoderArguments.push_back (OnnxSession::TensorView::floats (name.c_str(), &durationSeconds, { 1 }));
        else
        {
            error = "the note encoder asks for an input RVCARA does not know: " + juce::String (name);
            return {};
        }
    }

    const auto embedded = encoder.run (encoderArguments, { "x_seg", "x_est", "maskT" });

    if (embedded.size() != 3)
    {
        error = "note encoding failed: " + encoder.getError();
        return {};
    }

    const auto frameShape = embedded[2].GetTensorTypeAndShapeInfo().GetShape();
    const auto numFrames = frameShape.size() >= 2 ? frameShape[1] : frameShape.front();

    const auto featureShape = embedded[0].GetTensorTypeAndShapeInfo().GetShape();
    const auto numChannels = featureShape.size() >= 3 ? featureShape[2] : 0;

    if (numFrames <= 0 || numChannels <= 0)
        return {};

    const auto* segmentationFeatures = embedded[0].GetTensorData<float>();
    const auto* estimationFeatures = embedded[1].GetTensorData<float>();
    const auto* frameMask = asBytes (embedded[2].GetTensorData<bool>());

    const auto numFrameBytes = static_cast<std::size_t> (numFrames);

    const std::vector<std::uint8_t> knownBoundaries (numFrameBytes, 0);
    std::vector<std::uint8_t> boundaries (numFrameBytes, 0);

    const std::int64_t language = 0;
    const auto radius = boundaryRadius;
    const auto threshold = boundaryThreshold;
    auto diffusionTime = 0.0f;

    const auto numSteps = isDiffusion ? numDiffusionSteps : 1;

    for (int stepIndex = 0; stepIndex < numSteps; ++stepIndex)
    {
        diffusionTime = isDiffusion ? static_cast<float> (stepIndex) / static_cast<float> (numSteps)
                                    : 0.0f;

        std::vector<OnnxSession::TensorView> stepArguments;

        for (const auto& name : boundaryFinderInputs)
        {
            if (name == "x_seg")
                stepArguments.push_back (OnnxSession::TensorView::floats (
                    name.c_str(), segmentationFeatures, { 1, numFrames, numChannels }));
            else if (name == "maskT")
                stepArguments.push_back (OnnxSession::TensorView::booleans (name.c_str(), frameMask, { 1, numFrames }));
            else if (name == "known_boundaries")
                stepArguments.push_back (OnnxSession::TensorView::booleans (
                    name.c_str(), knownBoundaries.data(), { 1, numFrames }));
            else if (name == "prev_boundaries")
                stepArguments.push_back (OnnxSession::TensorView::booleans (
                    name.c_str(), boundaries.data(), { 1, numFrames }));
            else if (name == "threshold")
                stepArguments.push_back (OnnxSession::TensorView::floats (name.c_str(), &threshold, {}));
            else if (name == "radius")
                stepArguments.push_back (OnnxSession::TensorView::integers (name.c_str(), &radius, {}));
            else if (name == "t")
                stepArguments.push_back (OnnxSession::TensorView::floats (name.c_str(), &diffusionTime, { 1 }));
            else if (name == "language")
                stepArguments.push_back (OnnxSession::TensorView::integers (name.c_str(), &language, { 1 }));
            else
            {
                error = "the note segmenter asks for an input RVCARA does not know: " + juce::String (name);
                return {};
            }
        }

        const auto found = boundaryFinder.run (stepArguments, { "boundaries" });

        if (found.empty())
        {
            error = "note segmentation failed: " + boundaryFinder.getError();
            return {};
        }

        std::memcpy (boundaries.data(), found.front().GetTensorData<bool>(), numFrameBytes);
    }

    std::vector<OnnxSession::TensorView> durationArguments;

    for (const auto& name : durationMakerInputs)
    {
        if (name == "boundaries")
            durationArguments.push_back (OnnxSession::TensorView::booleans (
                name.c_str(), boundaries.data(), { 1, numFrames }));
        else if (name == "maskT")
            durationArguments.push_back (OnnxSession::TensorView::booleans (name.c_str(), frameMask, { 1, numFrames }));
        else if (name == "duration")
            durationArguments.push_back (OnnxSession::TensorView::floats (name.c_str(), &durationSeconds, { 1 }));
        else
        {
            error = "the note timer asks for an input RVCARA does not know: " + juce::String (name);
            return {};
        }
    }

    const auto timed = durationMaker.run (durationArguments, { "durations", "maskN" });

    if (timed.size() != 2)
    {
        error = "note timing failed: " + durationMaker.getError();
        return {};
    }

    const auto durationShape = timed[0].GetTensorTypeAndShapeInfo().GetShape();
    const auto numNotes = durationShape.size() >= 2 ? durationShape[1] : durationShape.front();

    if (numNotes <= 0)
        return {};

    const auto* noteDurations = timed[0].GetTensorData<float>();
    const auto* noteMask = asBytes (timed[1].GetTensorData<bool>());

    std::vector<OnnxSession::TensorView> namingArguments;
    const auto namingThreshold = presenceThreshold;

    for (const auto& name : pitchNamerInputs)
    {
        if (name == "x_est")
            namingArguments.push_back (OnnxSession::TensorView::floats (
                name.c_str(), estimationFeatures, { 1, numFrames, numChannels }));
        else if (name == "maskT")
            namingArguments.push_back (OnnxSession::TensorView::booleans (name.c_str(), frameMask, { 1, numFrames }));
        else if (name == "boundaries")
            namingArguments.push_back (OnnxSession::TensorView::booleans (
                name.c_str(), boundaries.data(), { 1, numFrames }));
        else if (name == "maskN")
            namingArguments.push_back (OnnxSession::TensorView::booleans (name.c_str(), noteMask, { 1, numNotes }));
        else if (name == "threshold")
            namingArguments.push_back (OnnxSession::TensorView::floats (name.c_str(), &namingThreshold, {}));
        else
        {
            error = "the note namer asks for an input RVCARA does not know: " + juce::String (name);
            return {};
        }
    }

    const auto named = pitchNamer.run (namingArguments, { "presence", "scores" });

    if (named.size() != 2)
    {
        error = "note naming failed: " + pitchNamer.getError();
        return {};
    }

    const auto* isVoiced = named[0].GetTensorData<bool>();
    const auto* midiNotes = named[1].GetTensorData<float>();

    std::vector<EditedNote> notes;
    notes.reserve (static_cast<std::size_t> (numNotes));

    auto cursorSeconds = startSeconds;

    for (std::int64_t noteIndex = 0; noteIndex < numNotes; ++noteIndex)
    {
        if (noteMask[noteIndex] == 0)
            break;

        EditedNote note;
        note.startSeconds = cursorSeconds;
        cursorSeconds += static_cast<double> (noteDurations[noteIndex]);
        note.endSeconds = cursorSeconds;
        note.isRest = ! isVoiced[noteIndex];

        const auto estimated = midiNotes[noteIndex];
        note.sungMidiNote = std::isfinite (estimated) ? std::clamp (estimated, 0.0f, 127.0f) : 60.0f;

        notes.push_back (note);
    }

    return notes;
}

std::vector<EditedNote> NoteSegmenter::segment (const float* samples,
                                                int numSamples,
                                                double sampleRate,
                                                const std::atomic<bool>& shouldAbort,
                                                juce::String& error) const
{
    if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return {};

    const SincResampler toModelRate { sampleRate, static_cast<double> (modelSampleRate) };
    const auto waveform = toModelRate.process (samples, numSamples);

    if (waveform.empty())
    {
        error = "resampling for note detection produced nothing";
        return {};
    }

    std::vector<EditedNote> notes;

    for (const auto& chunk : planChunks (waveform))
    {
        if (shouldAbort.load())
            return {};

        const auto chunkNumSamples = chunk.endSample - chunk.startSample;

        if (chunkNumSamples <= 0)
            continue;

        const auto startSeconds = static_cast<double> (chunk.startSample)
                                / static_cast<double> (modelSampleRate);

        auto chunkNotes = segmentChunk (waveform.data() + chunk.startSample,
                                        chunkNumSamples,
                                        startSeconds,
                                        error);

        if (error.isNotEmpty())
            return {};

        notes.insert (notes.end(), chunkNotes.begin(), chunkNotes.end());
    }

    return notes;
}
}
