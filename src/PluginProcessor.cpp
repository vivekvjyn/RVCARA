#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "DocumentController.h"

namespace rvcara
{

namespace
{
    /** Parameter ranges.

        The pitch range is a full two octaves either way: transposing a male source onto a
        female model, which is the common case for this kind of plugin, is routinely a
        twelve-semitone move, and the range has to allow the register change without the
        user running out of slider.
    */
    constexpr auto minimumPitchShiftSemitones = -24.0f;
    constexpr auto maximumPitchShiftSemitones = 24.0f;

    /** Consonant protection is defined only below 0.5; at and above it, it is off. */
    constexpr auto maximumConsonantProtection = 0.5f;

    constexpr auto minimumLatentNoiseSeed = 1;
    constexpr auto maximumLatentNoiseSeed = 9999;
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
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

PluginProcessor::PluginProcessor()
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

PluginProcessor::~PluginProcessor()
{
    for (const auto* parameterId : { ParameterId::pitchShiftSemitones,
                                     ParameterId::retrievalRatio,
                                     ParameterId::consonantProtection,
                                     ParameterId::envelopeFollowRatio,
                                     ParameterId::latentNoiseSeed,
                                     ParameterId::isBypassed })
        parameters.removeParameterListener (parameterId, this);
}

DocumentController* PluginProcessor::getConversionDocumentController() const
{
    // Two steps, because the two things are different objects. ARA hands the plugin its
    // own DocumentController; ours is a *specialisation* attached to it, and JUCE keeps a
    // mapping from one to the other.
    auto* araDocumentController = getDocumentController<ARA::PlugIn::DocumentController>();

    if (araDocumentController == nullptr)
        return nullptr;

    return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<DocumentController> (
        araDocumentController);
}

VoiceLoader& PluginProcessor::getVoiceLoader() noexcept
{
    // In ARA mode the document controller's loader is the one every instance in the session
    // shares, so the editor and the converter must both go through it rather than ours.
    if (auto* documentController = getConversionDocumentController())
        return documentController->getVoiceLoader();

    return insertVoiceLoader;
}

ConversionSettings PluginProcessor::getSettingsFromParameters() const
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

void PluginProcessor::setParametersFromSettings (const ConversionSettings& settings)
{
    const juce::ScopedValueSetter<bool> suppress { isSynchronisingParameters, true };

    const auto assign = [this] (const char* parameterId, float value)
    {
        if (auto* parameter = parameters.getParameter (parameterId))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    assign (ParameterId::pitchShiftSemitones, settings.pitchShiftSemitones);
    assign (ParameterId::retrievalRatio, settings.retrievalRatio);
    assign (ParameterId::consonantProtection, settings.consonantProtection);
    assign (ParameterId::envelopeFollowRatio, settings.envelopeFollowRatio);
    assign (ParameterId::latentNoiseSeed, static_cast<float> (settings.latentNoiseSeed));
    assign (ParameterId::isBypassed, settings.isBypassed ? 1.0f : 0.0f);
}

std::vector<ConversionModification*> PluginProcessor::getEditableModifications() const
{
    auto* documentController = getConversionDocumentController();

    if (documentController == nullptr)
        return {};

    // Prefer the regions this instance is actually responsible for, so a session with
    // several instances edits the right ones.
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

    // Before the host has assigned regions, fall back to the whole document so the editor
    // is not blank on first open.
    if (modifications.empty())
        return documentController->getModifications();

    return modifications;
}

void PluginProcessor::parameterChanged (const juce::String&, float)
{
    if (isSynchronisingParameters)
        return;

    const auto settings = getSettingsFromParameters();

    if (auto* documentController = getConversionDocumentController())
    {
        // Requeues a render only for modifications whose settings actually change, which
        // applySettings decides.
        for (auto* modification : getEditableModifications())
            documentController->applySettings (*modification, settings);

        return;
    }

    // Insert mode. The capture is already in hand, so a settings change can re-render
    // immediately rather than waiting for another pass of the transport.
    insertConverter.setSettings (settings);
    convertCapturedAudio();
}

// ==================================================================================

void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    if (prepareToPlayForARA (sampleRate,
                             maximumExpectedSamplesPerBlock,
                             getMainBusNumOutputChannels(),
                             getProcessingPrecision()))
        return;

    insertConverter.setSettings (getSettingsFromParameters());
    insertConverter.prepare (sampleRate, maximumExpectedSamplesPerBlock);

    // Load the voice as soon as the host means to use the plug-in, so that the first pass of
    // the transport is captured against a voice already in memory and converts the moment the
    // transport stops. Deliberately not done in the constructor: hosts instantiate plug-ins
    // while scanning, and a scan should not read a gigabyte of graph off the disk.
    insertVoiceLoader.requestDefaultVoice ([this] { convertCapturedAudio(); });
}

void PluginProcessor::convertCapturedAudio()
{
    if (insertConverter.getCapturedSeconds() > 0.0)
        insertConverter.requestConversion();
}

void PluginProcessor::releaseResources()
{
    if (releaseResourcesForARA())
        return;

    insertConverter.release();
}

double PluginProcessor::getTailLengthSeconds() const
{
    double tailLength = 0.0;
    getTailLengthSecondsForARA (tailLength);
    return tailLength;
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo, with matching input and output. The conversion itself is mono; the
    // input bus exists so the plugin can pass audio through when it is not bound to ARA.
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;

    const auto realtime = isNonRealtime() ? juce::AudioProcessor::Realtime::no
                                          : juce::AudioProcessor::Realtime::yes;

    if (processBlockForARA (buffer, realtime, getPlayHead()))
        return;

    // Insert mode: capture what goes past, and substitute the conversion where one exists.
    // Channels the input does not supply are cleared first so the converter is not writing
    // over stale data.
    for (auto channelIndex = getTotalNumInputChannels(); channelIndex < getTotalNumOutputChannels(); ++channelIndex)
        buffer.clear (channelIndex, 0, buffer.getNumSamples());

    juce::AudioPlayHead::PositionInfo positionInfo;

    if (auto* hostPlayHead = getPlayHead())
        if (const auto hostPosition = hostPlayHead->getPosition())
            positionInfo = *hostPosition;

    insertConverter.process (buffer, positionInfo);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor { *this };
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    // Only the parameters. The per-region settings and the voice choice live in the ARA
    // archive, which the host stores separately and which is the authority.
    if (auto state = parameters.copyState(); auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace rvcara

// ======================================================================================
// Plugin and ARA factory entry points.
//
// createPluginFilter is required of every JUCE plugin. createARAFactory is additionally
// required when IS_ARA_EFFECT is set, and is how the host discovers that this binary has
// an ARA personality and which document controller to instantiate for it.

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rvcara::PluginProcessor();
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<rvcara::DocumentController>();
}
