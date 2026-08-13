#include "model/VoiceModel.h"

#include "common/ConversionSettings.h"

#include <vector>

namespace rvcara
{
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

    if (! model->contentEncoder.load (directory.getChildFile (manifest.contentEncoderFile), numThreads))
    {
        error = model->contentEncoder.getError();
        return nullptr;
    }

    if (! model->pitchNetwork.load (directory.getChildFile (manifest.pitchEstimatorFile), numThreads))
    {
        error = model->pitchNetwork.getError();
        return nullptr;
    }

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
                                                              model->pitchNetwork);

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
