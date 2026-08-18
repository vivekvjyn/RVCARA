#include "ara/PlaybackRenderer.h"

#include "ara/ConversionModification.h"
#include "ara/DocumentController.h"

#include <algorithm>

namespace rvcara
{
namespace
{
    constexpr double readAheadSeconds = 2.0;
}

PlaybackRenderer::PlaybackRenderer (ARA::PlugIn::DocumentController* documentController,
                                    DocumentController& ownerToUse)
    : juce::ARAPlaybackRenderer (documentController),
      owner (ownerToUse)
{
}

void PlaybackRenderer::prepareToPlay (double sampleRate,
                                      int maximumSamplesPerBlock,
                                      int,
                                      juce::AudioProcessor::ProcessingPrecision,
                                      AlwaysNonRealtime alwaysNonRealtime)
{
    hostSampleRate = sampleRate;
    isAlwaysNonRealtime = alwaysNonRealtime == AlwaysNonRealtime::yes;

    sourceReaders.clear();

    for (const auto* playbackRegion : getPlaybackRegions())
    {
        auto* audioSource = playbackRegion->getAudioModification()->getAudioSource();

        if (sourceReaders.find (audioSource) != sourceReaders.end())
            continue;

        auto reader = std::make_unique<juce::ARAAudioSourceReader> (audioSource);

        SourceReader entry;

        if (isAlwaysNonRealtime)
        {
            entry.reader = std::move (reader);
        }
        else
        {
            const auto readAheadSize = std::max (4 * maximumSamplesPerBlock,
                                                 juce::roundToInt (readAheadSeconds * sampleRate));

            auto buffering = std::make_unique<juce::BufferingAudioReader> (reader.release(),
                                                                          *readAheadThread,
                                                                          readAheadSize);
            entry.buffering = buffering.get();
            entry.reader = std::move (buffering);
        }

        sourceReaders.emplace (audioSource, std::move (entry));
    }
}

void PlaybackRenderer::releaseResources()
{
    sourceReaders.clear();
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

        const auto playbackRange =
            playbackRegion->getSampleRange (hostSampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);

        auto renderRange = blockRange.getIntersectionWith (playbackRange);

        if (renderRange.isEmpty())
            continue;

        const juce::Range<juce::int64> modificationRange { playbackRegion->getStartInAudioModificationSamples(),
                                                           playbackRegion->getEndInAudioModificationSamples() };
        const auto modificationOffset = modificationRange.getStart() - playbackRange.getStart();

        renderRange = renderRange.getIntersectionWith (
            modificationRange.movedToStartAt (playbackRange.getStart()));

        if (renderRange.isEmpty())
            continue;

        const auto ratesMatch = audioSource->getSampleRate() == hostSampleRate;

        const auto numSamplesToRead = static_cast<int> (renderRange.getLength());
        const auto startInBuffer = static_cast<int> (renderRange.getStart() - blockRange.getStart());
        const auto startInSource = renderRange.getStart() + modificationOffset;

        auto renderedFromConversion = false;

        if (ratesMatch && ! modification->getSettings().isBypassed)
        {
            if (const auto conversion = modification->getConversion())
            {
                const auto available = conversion->getNumSamples() - static_cast<int> (startInSource);
                const auto numToCopy = std::min (numSamplesToRead, std::max (available, 0));

                if (numToCopy > 0)
                {
                    const auto* source = conversion->samples.data() + startInSource;

                    for (int channelIndex = 0; channelIndex < buffer.getNumChannels(); ++channelIndex)
                        buffer.copyFrom (channelIndex, startInBuffer, source, numToCopy);

                    if (numToCopy < numSamplesToRead)
                        for (int channelIndex = 0; channelIndex < buffer.getNumChannels(); ++channelIndex)
                            buffer.clear (channelIndex, startInBuffer + numToCopy, numSamplesToRead - numToCopy);

                    renderedFromConversion = true;
                }
            }
        }

        if (! renderedFromConversion
            && ! renderSourceAudio (*playbackRegion, buffer, startInBuffer, numSamplesToRead,
                                    startInSource, realtime))
        {
            wasSuccessful = false;
            continue;
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
