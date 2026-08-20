#include "model/VoiceModel.h"

#include "common/ConversionSettings.h"
#include "model/VoiceModelLibrary.h"

#include <algorithm>
#include <vector>

namespace rvcara
{
namespace
{
    /** @brief The names the two universal graphs carry upstream, which is what they are
               installed as when they are shared rather than copied into every voice.
    */
    constexpr const char* sharedContentEncoderFile = "hubert_base.onnx";
    constexpr const char* sharedPitchEstimatorFile = "rmvpe.onnx";

    /** @brief Finds a graph: the voice's own copy first, then the one installed beside the voices.
        @param directory   The voice's directory.
        @param named       The file the manifest asks for.
        @param sharedName  What the same graph is called upstream, when it is shared.
        @return The file to load, which may not exist.
    */
    juce::File findGraph (const juce::File& directory,
                          const juce::String& named,
                          const juce::String& sharedName)
    {
        if (const auto own = directory.getChildFile (named); own.existsAsFile())
            return own;

        if (const auto shared = VoiceModelLibrary::findAssetFile (named); shared.existsAsFile())
            return shared;

        return VoiceModelLibrary::findAssetFile (sharedName);
    }

    /** @brief Reports a shared graph that is not the one the manifest describes.
        @param session   The graph as loaded.
        @param file      Where it came from, so the message says which copy is wrong.
        @param wanted    The input name the manifest declares.
        @param role      What the graph is to the pipeline, for the message.
        @return An empty string when the graph matches, otherwise what is wrong with it.
    */
    juce::String describeMismatch (const OnnxSession& session,
                                   const juce::File& file,
                                   const std::string& wanted,
                                   const juce::String& role)
    {
        const auto names = session.getInputNames();

        if (std::find (names.begin(), names.end(), wanted) != names.end())
            return {};

        return "the " + role + " at " + file.getFullPathName() + " takes no input called "
             + juce::String (wanted) + ", so it is not the graph this voice was exported against";
    }
} // namespace

VoiceModel::~VoiceModel() = default;

ConversionSettings VoiceModel::getDefaultSettings() const noexcept
{
    ConversionSettings settings;
    settings.pitchShiftSemitones = manifest.defaultPitchShiftSemitones;
    settings.retrievalRatio = manifest.defaultRetrievalRatio;
    settings.consonantProtection = manifest.defaultConsonantProtection;
    settings.envelopeFollowRatio = manifest.defaultEnvelopeFollowRatio;
    settings.latentNoiseSeed = manifest.defaultLatentNoiseSeed;
    return settings;
}

std::unique_ptr<VoiceModel> VoiceModel::load (const juce::File& directory,
                                              int numThreads,
                                              juce::String& error)
{
    error.clear();

    auto model = std::unique_ptr<VoiceModel> (new VoiceModel);
    model->sourceDirectory = directory;

    auto parsed = ModelManifest::parse (directory.getChildFile ("manifest.json"), error);

    if (! parsed.has_value())
        return nullptr;

    model->manifest = std::move (*parsed);
    const auto& manifest = model->manifest;

    const auto contentEncoderFile = findGraph (directory, manifest.contentEncoderFile,
                                               sharedContentEncoderFile);

    model->contentEncoder = OnnxSession::getShared (contentEncoderFile, numThreads, error);

    if (model->contentEncoder == nullptr)
        return nullptr;

    error = describeMismatch (*model->contentEncoder, contentEncoderFile,
                              manifest.contentEncoderInput, "content encoder");

    if (error.isNotEmpty())
        return nullptr;

    const auto pitchEstimatorFile = findGraph (directory, manifest.pitchEstimatorFile,
                                               sharedPitchEstimatorFile);

    model->pitchNetwork = OnnxSession::getShared (pitchEstimatorFile, numThreads, error);

    if (model->pitchNetwork == nullptr)
        return nullptr;

    error = describeMismatch (*model->pitchNetwork, pitchEstimatorFile,
                              manifest.pitchEstimatorInput, "pitch estimator");

    if (error.isNotEmpty())
        return nullptr;

    if (! model->vocoder.load (directory.getChildFile (manifest.vocoderFile), numThreads))
    {
        error = model->vocoder.getError();
        return nullptr;
    }

    constexpr int expectedNumVocoderInputs = 6;

    if (static_cast<int> (manifest.vocoderInputs.size()) != expectedNumVocoderInputs)
    {
        error = "vocoder declares " + juce::String (static_cast<int> (manifest.vocoderInputs.size()))
              + " inputs, expected " + juce::String (expectedNumVocoderInputs);
        return nullptr;
    }

    model->melFilterBank = BinaryMatrix::load (directory.getChildFile (manifest.melFilterBankFile));

    if (! model->melFilterBank.isValid())
    {
        error = model->melFilterBank.getError();
        return nullptr;
    }

    const auto& mel = manifest.melConfiguration;

    if (model->melFilterBank.getNumRows() != mel.numMelBins
        || model->melFilterBank.getNumColumns() != mel.numBins)
    {
        error = "mel filter bank is " + juce::String (model->melFilterBank.getNumRows()) + " x "
              + juce::String (model->melFilterBank.getNumColumns()) + ", manifest declares "
              + juce::String (mel.numMelBins) + " x " + juce::String (mel.numBins);
        return nullptr;
    }

    std::vector<float> filterBankCopy (
        model->melFilterBank.getData(),
        model->melFilterBank.getData()
            + static_cast<std::size_t> (mel.numMelBins) * static_cast<std::size_t> (mel.numBins));

    model->melSpectrogram = std::make_unique<MelSpectrogram> (mel, std::move (filterBankCopy));

    model->pitchEstimator = std::make_unique<PitchEstimator> (model->manifest,
                                                              *model->melSpectrogram,
                                                              *model->pitchNetwork);

    model->inputFilter = ZeroPhaseFilter (manifest.highPassSections, manifest.highPassPadLength);

    if (manifest.hasRetrieval())
    {
        model->codebook = BinaryMatrix::load (directory.getChildFile (manifest.retrievalFile));

        if (! model->codebook.isValid())
        {
            error = model->codebook.getError();
            return nullptr;
        }

        if (model->codebook.getNumColumns() != manifest.featureDim)
        {
            error = "codebook holds " + juce::String (model->codebook.getNumColumns())
                  + "-D vectors, model expects " + juce::String (manifest.featureDim) + "-D";
            return nullptr;
        }

        FeatureRetriever::Configuration retrievalConfiguration;
        retrievalConfiguration.numNeighbours = manifest.numNeighbours;
        retrievalConfiguration.graphDegree = manifest.graphDegree;
        retrievalConfiguration.constructionCandidateListSize = manifest.constructionCandidateListSize;
        retrievalConfiguration.searchCandidateListSize = manifest.searchCandidateListSize;

        model->retriever = std::make_unique<FeatureRetriever>();

        const auto cacheFile = directory.getChildFile (
            "retrieval-" + juce::String (model->codebook.getNumRows()) + ".hnsw");

        if (! model->retriever->prepare (model->codebook, retrievalConfiguration, cacheFile))
        {
            model->retriever.reset();
        }
    }

    return model;
}
}
