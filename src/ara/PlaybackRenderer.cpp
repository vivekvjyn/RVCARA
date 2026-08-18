#include "ara/PlaybackRenderer.h"

#include "ara/ConversionModification.h"
#include "ara/DocumentController.h"

#include <algorithm>

namespace rvcara
{
namespace
{
    constexpr double readAheadSeconds = 2.0;

    constexpr double crossfadeSeconds = 0.01;
}

PlaybackRenderer::PlaybackRenderer (ARA::PlugIn::DocumentController* documentController,
                                    DocumentController& ownerToUse)
    : juce::ARAPlaybackRenderer (documentController),
      owner (ownerToUse)
{
}

PlaybackRenderer::~PlaybackRenderer()
{
    owner.forgetRenderer (this);
}

void PlaybackRenderer::prepareToPlay (double sampleRate,
                                      int maximumSamplesPerBlock,
                                      int,
                                      juce::AudioProcessor::ProcessingPrecision,
                                      AlwaysNonRealtime alwaysNonRealtime)
{
    hostSampleRate = sampleRate;
    maximumBlockSize = maximumSamplesPerBlock;
    isAlwaysNonRealtime = alwaysNonRealtime == AlwaysNonRealtime::yes;

    owner.setSessionSampleRate (this, sampleRate);

    sourceReaders.clear();

    for (const auto* playbackRegion : getPlaybackRegions())
        createSourceReader (playbackRegion->getAudioModification()->getAudioSource());
}

void PlaybackRenderer::createSourceReader (juce::ARAAudioSource* audioSource)
{
    if (audioSource == nullptr || sourceReaders.find (audioSource) != sourceReaders.end())
        return;

    auto reader = std::make_unique<juce::ARAAudioSourceReader> (audioSource);

    SourceReader entry;

    if (isAlwaysNonRealtime)
    {
        entry.reader = std::move (reader);
    }
    else
    {
        const auto readAheadSize = std::max (4 * maximumBlockSize,
                                             juce::roundToInt (readAheadSeconds * audioSource->getSampleRate()));

        auto buffering = std::make_unique<juce::BufferingAudioReader> (reader.release(),
                                                                      *readAheadThread,
                                                                      readAheadSize);
        entry.buffering = buffering.get();
        entry.reader = std::move (buffering);
    }

    sourceReaders.emplace (audioSource, std::move (entry));
}

void PlaybackRenderer::didAddPlaybackRegion (ARA::PlugIn::PlaybackRegion* playbackRegion) noexcept
{
    if (maximumBlockSize <= 0 || playbackRegion == nullptr)
        return;

    createSourceReader (
        static_cast<juce::ARAAudioSource*> (playbackRegion->getAudioModification()->getAudioSource()));
}

void PlaybackRenderer::releaseResources()
{
    sourceReaders.clear();
    maximumBlockSize = 0;
}

bool PlaybackRenderer::renderSourceAudio (juce::ARAPlaybackRegion& playbackRegion,
                                          juce::AudioBuffer<float>& buffer,
                                          int startInBuffer,
                                          int numSamplesToRead,
                                          juce::int64 startInSource,
                                          juce::AudioProcessor::Realtime realtime)
{
    auto* audioSource = playbackRegion.getAudioModification()->getAudioSource();
    const auto found = sourceReaders.find (audioSource);

    if (found == sourceReaders.end() || found->second.reader == nullptr)
        return false;

    found->second.setReadTimeout (realtime == juce::AudioProcessor::Realtime::no ? 100 : 0);

    return found->second.reader->read (&buffer, startInBuffer, numSamplesToRead, startInSource, true, true);
}

bool PlaybackRenderer::processBlock (juce::AudioBuffer<float>& buffer,
                                     juce::AudioProcessor::Realtime realtime,
                                     const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const auto lock = owner.getProcessingLock();

    if (! lock.isLocked())
    {
        buffer.clear();
        return true;
    }

    const auto numSamples = buffer.getNumSamples();

    if (! positionInfo.getIsPlaying())
    {
        buffer.clear();
        return true;
    }

    const auto timeInSamples = positionInfo.getTimeInSamples().orFallback (0);
    const auto blockRange = juce::Range<juce::int64>::withStartAndLength (timeInSamples, numSamples);

    auto didRenderAnything = false;
    auto wasSuccessful = true;

    for (auto* playbackRegion : getPlaybackRegions())
    {
        auto* modification = playbackRegion->getAudioModification<ConversionModification>();
        auto* audioSource = modification->getAudioSource();
        const auto sourceSampleRate = audioSource->getSampleRate();

        if (sourceSampleRate <= 0.0)
            continue;

        const auto playbackRange =
            playbackRegion->getSampleRange (hostSampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);

        auto renderRange = blockRange.getIntersectionWith (playbackRange);

        if (renderRange.isEmpty())
            continue;

        const juce::Range<juce::int64> modificationRange {
            ARA::samplePositionAtTime (playbackRegion->getStartInAudioModificationTime(), hostSampleRate),
            ARA::samplePositionAtTime (playbackRegion->getEndInAudioModificationTime(), hostSampleRate) };

        const auto modificationOffset = modificationRange.getStart() - playbackRange.getStart();

        renderRange = renderRange.getIntersectionWith (
            modificationRange.movedToStartAt (playbackRange.getStart()));

        if (renderRange.isEmpty())
            continue;

        const auto numSamplesToRead = static_cast<int> (renderRange.getLength());
        const auto startInBuffer = static_cast<int> (renderRange.getStart() - blockRange.getStart());
        const auto startInConversion = renderRange.getStart() + modificationOffset;

        const auto conversion = modification->getSettings().isBypassed ? ConversionPointer {}
                                                                       : modification->getConversion();

        const auto hasConversion = conversion != nullptr
                                && startInConversion >= 0
                                && juce::approximatelyEqual (conversion->sampleRate, hostSampleRate);

        const auto numConversionSamples = hasConversion ? conversion->getNumSamples() : 0;

        const auto numFromConversion = hasConversion
            ? std::clamp (numConversionSamples - static_cast<int> (startInConversion), 0, numSamplesToRead)
            : 0;

        const auto fadeSamples = hasConversion && conversion->isPartial
                               ? juce::roundToInt (crossfadeSeconds * hostSampleRate)
                               : 0;

        const auto firstSourceSample = std::clamp (
            static_cast<int> (numConversionSamples - fadeSamples - startInConversion),
            0,
            numFromConversion);

        if (firstSourceSample < numSamplesToRead)
        {
            const auto startOfRemainder = startInConversion + firstSourceSample;
            const auto startInSource = ARA::samplePositionAtTime (
                static_cast<double> (startOfRemainder) / hostSampleRate, sourceSampleRate);

            if (! renderSourceAudio (*playbackRegion,
                                     buffer,
                                     startInBuffer + firstSourceSample,
                                     numSamplesToRead - firstSourceSample,
                                     startInSource,
                                     realtime))
            {
                wasSuccessful = false;
                continue;
            }
        }

        if (numFromConversion > 0)
        {
            const auto* source = conversion->samples.data() + startInConversion;

            for (int channelIndex = 0; channelIndex < buffer.getNumChannels(); ++channelIndex)
            {
                if (fadeSamples <= 0)
                {
                    buffer.copyFrom (channelIndex, startInBuffer, source, numFromConversion);
                    continue;
                }

                auto* destination = buffer.getWritePointer (channelIndex, startInBuffer);

                for (int sampleIndex = 0; sampleIndex < numFromConversion; ++sampleIndex)
                {
                    const auto gain = static_cast<float> (juce::jlimit (
                        0.0,
                        1.0,
                        static_cast<double> (numConversionSamples - startInConversion - sampleIndex)
                            / static_cast<double> (fadeSamples)));

                    destination[sampleIndex] = source[sampleIndex] * gain
                                             + destination[sampleIndex] * (1.0f - gain);
                }
            }
        }

        if (! didRenderAnything)
        {
            if (startInBuffer > 0)
                buffer.clear (0, startInBuffer);

            const auto endInBuffer = startInBuffer + numSamplesToRead;

            if (endInBuffer < numSamples)
                buffer.clear (endInBuffer, numSamples - endInBuffer);

            didRenderAnything = true;
        }
    }

    if (! didRenderAnything)
        buffer.clear();

    return wasSuccessful;
}
}
