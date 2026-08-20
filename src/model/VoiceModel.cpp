#include "model/VoiceModel.h"

#include "common/ConversionSettings.h"
#include "model/VoiceModelLibrary.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace rvcara
{
namespace
{
    /** @brief The names the two universal graphs carry upstream, used when one is installed
               loose beside the voices rather than in a directory of its own.
    */
    constexpr const char* sharedContentEncoderFile = "contentvec.onnx";
    constexpr const char* sharedPitchEstimatorFile = "rmvpe.onnx";

    /** @brief What RVC calls the content encoder, which is a misnomer worth still answering to. */
    constexpr const char* legacyContentEncoderFile = "hubert_base.onnx";

    constexpr const char* sharedConfigurationFile = "config.json";

    /** @brief Where a graph came from and, when it is a shared one, what it says about itself. */
    template <typename Manifest>
    struct ResolvedGraph
    {
        juce::File file;
        std::optional<Manifest> shared;
    };

    /** @brief Finds a graph: the voice's own copy first, then the one installed beside the voices.

        A shared graph lives in a directory named for its model and carrying a config.json that
        describes it, which is the description used in place of the voice's. The loose forms are
        accepted too, so a graph moved out of a voice directory by hand still loads.

        @param directory   The voice's directory.
        @param named       The file the voice's manifest asks for.
        @param sharedName  What the same graph is called upstream.
        @param error       Set when a shared configuration was found but could not be read.
        @return The file to load, which may not exist, and the configuration beside it.
    */
    template <typename Manifest>
    ResolvedGraph<Manifest> findGraph (const juce::File& directory,
                                       const juce::String& named,
                                       const juce::String& sharedName,
                                       juce::String& error)
    {
        if (const auto own = directory.getChildFile (named); own.existsAsFile())
            return { own, std::nullopt };

        const auto assetDirectory = VoiceModelLibrary::findAssetDirectory (Manifest::directoryName);

        if (const auto configuration = assetDirectory.getChildFile (sharedConfigurationFile);
            configuration.existsAsFile())
        {
            auto shared = Manifest::parse (configuration, error);

            if (! shared.has_value())
                return {};

            return { assetDirectory.getChildFile (shared->graphFile), std::move (shared) };
        }

        for (const auto& loose : { named, sharedName })
            if (const auto beside = VoiceModelLibrary::findAssetFile (loose); beside.existsAsFile())
                return { beside, std::nullopt };

        return { VoiceModelLibrary::findAssetFile (sharedName), std::nullopt };
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

    auto contentEncoder = findGraph<ContentEncoderManifest> (
        directory, manifest.contentEncoderFile, sharedContentEncoderFile, error);

    if (error.isEmpty() && ! contentEncoder.file.existsAsFile())
        contentEncoder = findGraph<ContentEncoderManifest> (
            directory, manifest.contentEncoderFile, legacyContentEncoderFile, error);

    if (error.isNotEmpty())
        return nullptr;

    if (contentEncoder.shared.has_value() && ! model->manifest.adopt (*contentEncoder.shared, error))
        return nullptr;

    model->contentEncoder = OnnxSession::getShared (contentEncoder.file, numThreads, error);

    if (model->contentEncoder == nullptr)
        return nullptr;

    error = describeMismatch (*model->contentEncoder, contentEncoder.file,
                              manifest.contentEncoderInput, "content encoder");

    if (error.isNotEmpty())
        return nullptr;

    const auto pitchEstimator = findGraph<PitchEstimatorManifest> (
        directory, manifest.pitchEstimatorFile, sharedPitchEstimatorFile, error);

    if (error.isNotEmpty())
        return nullptr;

    if (pitchEstimator.shared.has_value() && ! model->manifest.adopt (*pitchEstimator.shared, error))
        return nullptr;

    model->pitchNetwork = OnnxSession::getShared (pitchEstimator.file, numThreads, error);

    if (model->pitchNetwork == nullptr)
        return nullptr;

    error = describeMismatch (*model->pitchNetwork, pitchEstimator.file,
                              manifest.pitchEstimatorInput, "pitch estimator");

    if (error.isNotEmpty())
        return nullptr;

    if (! model->synthesizer.load (directory.getChildFile (manifest.synthesizerFile), numThreads))
    {
        error = model->synthesizer.getError();
        return nullptr;
    }

    constexpr int expectedNumSynthesizerInputs = 6;

    if (static_cast<int> (manifest.synthesizerInputs.size()) != expectedNumSynthesizerInputs)
    {
        error = "the synthesiser declares "
              + juce::String (static_cast<int> (manifest.synthesizerInputs.size()))
              + " inputs, expected " + juce::String (expectedNumSynthesizerInputs);
        return nullptr;
    }

    // The filter bank belongs to the estimator's front end, so it is looked for beside the graph
    // that wants it before the voice that used to carry a copy.
    auto filterBankFile = pitchEstimator.file.getParentDirectory().getChildFile (manifest.melFilterBankFile);

    if (! filterBankFile.existsAsFile())
        filterBankFile = directory.getChildFile (manifest.melFilterBankFile);

    model->melFilterBank = BinaryMatrix::load (filterBankFile);

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
