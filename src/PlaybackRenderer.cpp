#include "PlaybackRenderer.h"

#include "ConversionModification.h"
#include "DocumentController.h"

#include <algorithm>

namespace rvcara
{

namespace
{
    /** Seconds of read-ahead for the buffering readers.

        Only used for the dry fallback, so it needs to cover a hiccup rather than a whole
        region.
    */
    constexpr double readAheadSeconds = 2.0;
} // namespace

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
            // Offline: the host is happy to wait, so read directly and skip the thread.
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
    // A try-lock, not a lock. The model may be being updated on the message thread, and
    // waiting for it here would risk a dropout far worse than a block of silence.
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

        // Region borders in song time, intersected with this block.
        const auto playbackRange =
            playbackRegion->getSampleRange (hostSampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);

        auto renderRange = blockRange.getIntersectionWith (playbackRange);

        if (renderRange.isEmpty())
            continue;

        // Map song time onto the modification's own timeline. RVCARA does not time-stretch,
        // so this is a constant offset; a plugin that did would have to resample here.
        const juce::Range<juce::int64> modificationRange { playbackRegion->getStartInAudioModificationSamples(),
                                                           playbackRegion->getEndInAudioModificationSamples() };
        const auto modificationOffset = modificationRange.getStart() - playbackRange.getStart();

        renderRange = renderRange.getIntersectionWith (
            modificationRange.movedToStartAt (playbackRange.getStart()));

        if (renderRange.isEmpty())
            continue;

        // The conversion is cached at the audio source's own rate, which makes this mapping
        // the identity. A host running at a different rate to the source would need a
        // resample per block, which is not worth doing on the audio thread — so that case
        // falls through to the dry path instead of producing a pitch-shifted conversion.
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

                    // The conversion is mono; every output channel gets the same signal.
                    for (int channelIndex = 0; channelIndex < buffer.getNumChannels(); ++channelIndex)
                        buffer.copyFrom (channelIndex, startInBuffer, source, numToCopy);

                    // A conversion can be a sample or two shorter than the region after
                    // rate conversion; clear rather than leave the tail undefined.
                    if (numToCopy < numSamplesToRead)
                        for (int channelIndex = 0; channelIndex < buffer.getNumChannels(); ++channelIndex)
                            buffer.clear (channelIndex, startInBuffer + numToCopy, numSamplesToRead - numToCopy);

                    renderedFromConversion = true;
                }
            }
        }

        if (! renderedFromConversion)
        {
            // Dry fallback: bypassed, still rendering, failed, or a sample-rate mismatch.
            if (audioSource->getChannelCount() != buffer.getNumChannels()
                || ! renderSourceAudio (*playbackRegion, buffer, startInBuffer, numSamplesToRead,
                                        startInSource, realtime))
            {
                wasSuccessful = false;
                continue;
            }
        }

        if (! didRenderAnything)
        {
            // Clear whatever this region does not cover, once.
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

} // namespace rvcara
