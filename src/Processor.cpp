#include "Processor.h"

#include "Editor.h"

#include "ara/DocumentController.h"

namespace rvcara
{
namespace
{
    constexpr auto minimumPitchShiftSemitones = -24.0f;
    constexpr auto maximumPitchShiftSemitones = 24.0f;

    constexpr auto maximumConsonantProtection = 0.5f;

    constexpr auto minimumLatentNoiseSeed = 1;
    constexpr auto maximumLatentNoiseSeed = 9999;

    constexpr int settleMilliseconds = 150;
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createParameterLayout()
{
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterId::pitchShiftSemitones, 1 },
        "Pitch",
        juce::NormalisableRange<float> { minimumPitchShiftSemitones, maximumPitchShiftSemitones, 0.01f },
        0.0f,
        Attributes {}.withLabel ("st")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterId::retrievalRatio, 1 },
        "Timbre",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.75f,
        Attributes {}.withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterId::consonantProtection, 1 },
        "Consonants",
        juce::NormalisableRange<float> { 0.0f, maximumConsonantProtection, 0.001f },
        0.33f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterId::envelopeFollowRatio, 1 },
        "Dynamics",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterId::latentNoiseSeed, 1 },
        "Variation",
        minimumLatentNoiseSeed,
        maximumLatentNoiseSeed,
        1));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParameterId::isBypassed, 1 },
        "Bypass",
        false));

    return layout;
}

Processor::Processor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RVCARA", createParameterLayout())
{
    insertConverter.setVoiceLoader (&insertVoiceLoader);

    for (const auto* parameterId : { ParameterId::pitchShiftSemitones,
                                     ParameterId::retrievalRatio,
                                     ParameterId::consonantProtection,
                                     ParameterId::envelopeFollowRatio,
                                     ParameterId::latentNoiseSeed,
                                     ParameterId::isBypassed })
        parameters.addParameterListener (parameterId, this);
}

Processor::~Processor()
{
    cancelPendingUpdate();
    stopTimer();

    for (const auto* parameterId : { ParameterId::pitchShiftSemitones,
                                     ParameterId::retrievalRatio,
                                     ParameterId::consonantProtection,
                                     ParameterId::envelopeFollowRatio,
                                     ParameterId::latentNoiseSeed,
                                     ParameterId::isBypassed })
        parameters.removeParameterListener (parameterId, this);
}

DocumentController* Processor::getConversionDocumentController() const
{
    auto* araDocumentController = getDocumentController<ARA::PlugIn::DocumentController>();

    if (araDocumentController == nullptr)
        return nullptr;

    return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<DocumentController> (
        araDocumentController);
}

VoiceLoader& Processor::getVoiceLoader() noexcept
{
    if (auto* documentController = getConversionDocumentController())
        return documentController->getVoiceLoader();

    return insertVoiceLoader;
}

ConversionSettings Processor::getSettingsFromParameters() const
{
    const auto valueOf = [this] (const char* parameterId)
    {
        const auto* value = parameters.getRawParameterValue (parameterId);
        return value != nullptr ? value->load() : 0.0f;
    };

    ConversionSettings settings;
    settings.pitchShiftSemitones = valueOf (ParameterId::pitchShiftSemitones);
    settings.retrievalRatio = valueOf (ParameterId::retrievalRatio);
    settings.consonantProtection = valueOf (ParameterId::consonantProtection);
    settings.envelopeFollowRatio = valueOf (ParameterId::envelopeFollowRatio);
    settings.latentNoiseSeed = static_cast<std::int32_t> (valueOf (ParameterId::latentNoiseSeed));
    settings.isBypassed = valueOf (ParameterId::isBypassed) > 0.5f;

    return settings;
}

std::vector<ConversionModification*> Processor::getEditableModifications() const
{
    auto* documentController = getConversionDocumentController();

    if (documentController == nullptr)
        return {};

    std::vector<ConversionModification*> modifications;

    if (auto* playbackRenderer = getPlaybackRenderer<juce::ARAPlaybackRenderer>())
    {
        for (auto* playbackRegion : playbackRenderer->getPlaybackRegions())
        {
            auto* modification = playbackRegion->getAudioModification<ConversionModification>();

            if (std::find (modifications.begin(), modifications.end(), modification) == modifications.end())
                modifications.push_back (modification);
        }
    }

    if (auto* editorRenderer = getEditorRenderer<juce::ARAEditorRenderer>())
    {
        for (auto* playbackRegion : editorRenderer->getPlaybackRegions())
        {
            auto* modification = playbackRegion->getAudioModification<ConversionModification>();

            if (std::find (modifications.begin(), modifications.end(), modification) == modifications.end())
                modifications.push_back (modification);
        }
    }

    if (modifications.empty())
        return documentController->getModifications();

    return modifications;
}

void Processor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();
}

void Processor::handleAsyncUpdate()
{
    startTimer (settleMilliseconds);
}

void Processor::timerCallback()
{
    stopTimer();
    applySettings();
}

void Processor::applySettings()
{
    const auto settings = getSettingsFromParameters();

    if (auto* documentController = getConversionDocumentController())
    {
        for (auto* modification : getEditableModifications())
            documentController->applySettings (*modification, settings);

        return;
    }

    insertConverter.setSettings (settings);
    convertCapturedAudio();
}

void Processor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    if (prepareToPlayForARA (sampleRate,
                             maximumExpectedSamplesPerBlock,
                             getMainBusNumOutputChannels(),
                             getProcessingPrecision()))
        return;

    insertConverter.setSettings (getSettingsFromParameters());
    insertConverter.prepare (sampleRate, maximumExpectedSamplesPerBlock);

    insertVoiceLoader.requestDefaultVoice ([this] { convertCapturedAudio(); });
}

void Processor::convertCapturedAudio()
{
    if (insertConverter.getCapturedSeconds() > 0.0)
        insertConverter.requestConversion();
}

void Processor::releaseResources()
{
    if (releaseResourcesForARA())
        return;

    insertConverter.release();
}

double Processor::getTailLengthSeconds() const
{
    double tailLength = 0.0;
    getTailLengthSecondsForARA (tailLength);
    return tailLength;
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

double Processor::getPlayheadInModification (const ConversionModification& modification) const
{
    const auto hostSeconds = hostPositionSeconds.load (std::memory_order_relaxed);

    if (hostSeconds < 0.0)
        return -1.0;

    const auto findIn = [&] (const auto* renderer)
    {
        if (renderer == nullptr)
            return -1.0;

        for (const auto* playbackRegion : renderer->getPlaybackRegions())
        {
            if (playbackRegion->template getAudioModification<ConversionModification>() != &modification)
                continue;

            const auto intoRegion = hostSeconds - playbackRegion->getStartInPlaybackTime();

            if (intoRegion < 0.0 || intoRegion > playbackRegion->getDurationInPlaybackTime())
                continue;

            return playbackRegion->getStartInAudioModificationTime() + intoRegion;
        }

        return -1.0;
    };

    if (const auto found = findIn (getPlaybackRenderer<juce::ARAPlaybackRenderer>()); found >= 0.0)
        return found;

    return findIn (getEditorRenderer<juce::ARAEditorRenderer>());
}

bool Processor::canControlTransport() const
{
    if (auto* hostPlayHead = const_cast<Processor*> (this)->getPlayHead())
        return hostPlayHead->canControlTransport();

    return false;
}

void Processor::setTransportPlaying (bool shouldPlay)
{
    if (auto* hostPlayHead = getPlayHead(); hostPlayHead != nullptr && hostPlayHead->canControlTransport())
        hostPlayHead->transportPlay (shouldPlay);
}

void Processor::rewindTransport()
{
    if (auto* hostPlayHead = getPlayHead(); hostPlayHead != nullptr && hostPlayHead->canControlTransport())
        hostPlayHead->transportRewind();
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;

    {
        auto seconds = -1.0;

        if (auto* hostPlayHead = getPlayHead())
        {
            if (const auto hostPosition = hostPlayHead->getPosition())
            {
                if (hostPosition->getIsPlaying())
                    if (const auto timeInSeconds = hostPosition->getTimeInSeconds())
                        seconds = *timeInSeconds;

                if (const auto bpm = hostPosition->getBpm())
                    hostTempo.store (*bpm, std::memory_order_relaxed);

                if (const auto signature = hostPosition->getTimeSignature())
                    hostBeatsPerBar.store (signature->numerator, std::memory_order_relaxed);
            }
        }

        hostPositionSeconds.store (seconds, std::memory_order_relaxed);
    }

    const auto realtime = isNonRealtime() ? juce::AudioProcessor::Realtime::no
                                          : juce::AudioProcessor::Realtime::yes;

    if (processBlockForARA (buffer, realtime, getPlayHead()))
        return;

    for (auto channelIndex = getTotalNumInputChannels(); channelIndex < getTotalNumOutputChannels(); ++channelIndex)
        buffer.clear (channelIndex, 0, buffer.getNumSamples());

    juce::AudioPlayHead::PositionInfo positionInfo;

    if (auto* hostPlayHead = getPlayHead())
        if (const auto hostPosition = hostPlayHead->getPosition())
            positionInfo = *hostPosition;

    insertConverter.process (buffer, positionInfo);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor { *this };
}

void Processor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto state = parameters.copyState(); auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rvcara::Processor();
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<rvcara::DocumentController>();
}
