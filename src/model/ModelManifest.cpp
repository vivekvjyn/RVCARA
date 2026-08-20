#include "model/ModelManifest.h"

namespace rvcara
{
namespace
{
    const juce::DynamicObject* requireObject (const juce::DynamicObject& parent,
                                              const juce::Identifier& key,
                                              juce::String& error)
    {
        const auto value = parent.getProperty (key);

        if (auto* object = value.getDynamicObject())
            return object;

        if (error.isEmpty())
            error = "manifest is missing the \"" + key.toString() + "\" section";

        return nullptr;
    }

    template <typename Type>
    Type requireNumber (const juce::DynamicObject* object,
                        const juce::Identifier& key,
                        juce::String& error,
                        const juce::String& section)
    {
        if (object == nullptr)
            return Type {};

        const auto value = object->getProperty (key);

        if (value.isVoid() || value.isUndefined())
        {
            if (error.isEmpty())
                error = "manifest is missing " + section + "." + key.toString();

            return Type {};
        }

        return static_cast<Type> (static_cast<double> (value));
    }

    template <typename Type>
    Type optionalNumber (const juce::DynamicObject* object, const juce::Identifier& key, Type fallback)
    {
        if (object == nullptr)
            return fallback;

        const auto value = object->getProperty (key);

        if (value.isVoid() || value.isUndefined())
            return fallback;

        return static_cast<Type> (static_cast<double> (value));
    }

    bool optionalBool (const juce::DynamicObject* object, const juce::Identifier& key, bool fallback)
    {
        if (object == nullptr)
            return fallback;

        const auto value = object->getProperty (key);
        return value.isVoid() || value.isUndefined() ? fallback : static_cast<bool> (value);
    }

    /** @brief Opens a JSON object and checks the schema version it declares. */
    juce::var openConfiguration (const juce::File& file,
                                 int supportedSchemaVersion,
                                 juce::String& error)
    {
        error.clear();

        if (! file.existsAsFile())
        {
            error = "no configuration at " + file.getFullPathName();
            return {};
        }

        const auto parsed = juce::JSON::parse (file.loadFileAsString());

        if (parsed.getDynamicObject() == nullptr)
        {
            error = file.getFullPathName() + " is not a JSON object";
            return {};
        }

        const auto schemaVersion = optionalNumber<int> (parsed.getDynamicObject(), "schemaVersion", 0);

        if (schemaVersion != supportedSchemaVersion)
        {
            error = file.getFullPathName() + " declares schema version " + juce::String (schemaVersion)
                  + "; this build supports version " + juce::String (supportedSchemaVersion);
            return {};
        }

        return parsed;
    }

    /** @brief Reads the file and the two tensor names a shared model's graph section carries. */
    bool readSharedGraph (const juce::DynamicObject& root,
                          juce::String& file,
                          std::string& inputName,
                          std::string& outputName,
                          juce::String& error)
    {
        const auto* graph = requireObject (root, "graph", error);

        if (graph == nullptr)
            return false;

        file = graph->getProperty ("file").toString();
        inputName = graph->getProperty ("input").toString().toRawUTF8();
        outputName = graph->getProperty ("output").toString().toRawUTF8();

        if (file.isEmpty() || inputName.empty() || outputName.empty())
        {
            error = "the graph section needs a file, an input and an output";
            return false;
        }

        return true;
    }

    bool readGraph (const juce::DynamicObject* graphs,
                    const juce::Identifier& role,
                    juce::String& file,
                    std::vector<std::string>& inputNames,
                    std::string& outputName,
                    juce::String& error)
    {
        const auto entryValue = graphs != nullptr ? graphs->getProperty (role) : juce::var();
        auto* entry = entryValue.getDynamicObject();

        if (entry == nullptr)
        {
            if (error.isEmpty())
                error = "manifest has no graph entry for \"" + role.toString() + "\"";

            return false;
        }

        file = entry->getProperty ("file").toString();

        if (file.isEmpty())
        {
            if (error.isEmpty())
                error = "graph \"" + role.toString() + "\" has no file";

            return false;
        }

        const auto readNames = [] (const juce::var& listValue, std::vector<std::string>& destination)
        {
            destination.clear();

            if (const auto* list = listValue.getArray())
                for (const auto& element : *list)
                    if (auto* description = element.getDynamicObject())
                        destination.emplace_back (description->getProperty ("name").toString().toRawUTF8());
        };

        readNames (entry->getProperty ("inputs"), inputNames);

        std::vector<std::string> outputNames;
        readNames (entry->getProperty ("outputs"), outputNames);

        if (inputNames.empty() || outputNames.empty())
        {
            if (error.isEmpty())
                error = "graph \"" + role.toString() + "\" declares no inputs or outputs";

            return false;
        }

        outputName = outputNames.front();
        return true;
    }
}

std::optional<ContentEncoderManifest> ContentEncoderManifest::parse (const juce::File& file,
                                                                     juce::String& error)
{
    const auto parsed = openConfiguration (file, supportedSchemaVersion, error);
    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
        return std::nullopt;

    ContentEncoderManifest encoder;

    if (! readSharedGraph (*root, encoder.graphFile, encoder.inputName, encoder.outputName, error))
        return std::nullopt;

    encoder.sampleRate = requireNumber<int> (root, "sampleRate", error, "contentEncoder");
    encoder.frameRate = requireNumber<int> (root, "frameRate", error, "contentEncoder");
    encoder.featureDim = requireNumber<int> (root, "featureDim", error, "contentEncoder");
    encoder.minimumNumSamples = optionalNumber<int> (root, "minimumNumSamples", 400);

    if (error.isNotEmpty())
        return std::nullopt;

    return encoder;
}

std::optional<PitchEstimatorManifest> PitchEstimatorManifest::parse (const juce::File& file,
                                                                     juce::String& error)
{
    const auto parsed = openConfiguration (file, supportedSchemaVersion, error);
    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
        return std::nullopt;

    PitchEstimatorManifest estimator;

    if (! readSharedGraph (*root, estimator.graphFile, estimator.inputName, estimator.outputName, error))
        return std::nullopt;

    const auto* frontEnd = requireObject (*root, "frontEnd", error);
    estimator.sampleRate = requireNumber<int> (frontEnd, "sampleRate", error, "frontEnd");
    estimator.filterBankFile = frontEnd != nullptr ? frontEnd->getProperty ("filterBankFile").toString()
                                                   : juce::String();

    auto& mel = estimator.melConfiguration;
    mel.fftSize = requireNumber<int> (frontEnd, "fftSize", error, "frontEnd");
    mel.windowSize = optionalNumber<int> (frontEnd, "windowSize", mel.fftSize);
    mel.hopSizeInSamples = requireNumber<int> (frontEnd, "hopSizeInSamples", error, "frontEnd");
    mel.numMelBins = requireNumber<int> (frontEnd, "numMelBins", error, "frontEnd");
    mel.numBins = optionalNumber<int> (frontEnd, "numBins", mel.fftSize / 2 + 1);
    mel.magnitudeFloor = optionalNumber<float> (frontEnd, "magnitudeFloor", 1.0e-5f);
    mel.isCentred = optionalBool (frontEnd, "isCentred", true);

    const auto* decoder = requireObject (*root, "decoder", error);
    estimator.numPitchBins = requireNumber<int> (decoder, "numPitchBins", error, "decoder");
    estimator.centsOrigin = requireNumber<double> (decoder, "centsOrigin", error, "decoder");
    estimator.centsPerBin = requireNumber<double> (decoder, "centsPerBin", error, "decoder");
    estimator.centsReferenceHz = optionalNumber<double> (decoder, "centsReferenceHz", 10.0);
    estimator.localAverageRadius = optionalNumber<int> (decoder, "localAverageRadius", 4);
    estimator.salienceThreshold = optionalNumber<float> (decoder, "salienceThreshold", 0.03f);
    estimator.frameCountMultiple = optionalNumber<int> (decoder, "frameCountMultiple", 32);
    estimator.minimumFrequencyHz = optionalNumber<double> (decoder, "minimumFrequencyHz", 50.0);
    estimator.maximumFrequencyHz = optionalNumber<double> (decoder, "maximumFrequencyHz", 1100.0);

    if (estimator.filterBankFile.isEmpty())
        error = "the front end section names no filter bank";

    if (error.isNotEmpty())
        return std::nullopt;

    return estimator;
}

bool ModelManifest::adopt (const ContentEncoderManifest& encoder, juce::String& error)
{
    if (encoder.featureDim != featureDim)
    {
        error = "the installed content encoder returns " + juce::String (encoder.featureDim)
              + "-D features and " + name + " was trained on " + juce::String (featureDim) + "-D";
        return false;
    }

    contentEncoderInput = encoder.inputName;
    contentEncoderOutput = encoder.outputName;
    contentSampleRate = encoder.sampleRate;
    contentFrameRate = encoder.frameRate;
    contentMinimumNumSamples = encoder.minimumNumSamples;

    return true;
}

bool ModelManifest::adopt (const PitchEstimatorManifest& estimator, juce::String& error)
{
    pitchEstimatorInput = estimator.inputName;
    pitchEstimatorOutput = estimator.outputName;
    pitchSampleRate = estimator.sampleRate;
    melConfiguration = estimator.melConfiguration;
    melFilterBankFile = estimator.filterBankFile;

    numPitchBins = estimator.numPitchBins;
    centsOrigin = estimator.centsOrigin;
    centsPerBin = estimator.centsPerBin;
    centsReferenceHz = estimator.centsReferenceHz;
    localAverageRadius = estimator.localAverageRadius;
    salienceThreshold = estimator.salienceThreshold;
    frameCountMultiple = estimator.frameCountMultiple;
    pitchMinimumHz = estimator.minimumFrequencyHz;
    pitchMaximumHz = estimator.maximumFrequencyHz;

    return checkFrameRates (error);
}

bool ModelManifest::checkFrameRates (juce::String& error) const
{
    if (contentFrameRate <= 0 || getPitchFrameRate() % contentFrameRate != 0)
    {
        error = "content frame rate " + juce::String (contentFrameRate)
              + " Hz does not divide the conditioning rate "
              + juce::String (getPitchFrameRate()) + " Hz";
        return false;
    }

    return true;
}

std::optional<ModelManifest> ModelManifest::parse (const juce::File& file, juce::String& error)
{
    error.clear();

    if (! file.existsAsFile())
    {
        error = "no manifest at " + file.getFullPathName();
        return std::nullopt;
    }

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
    {
        error = file.getFileName() + " is not a JSON object";
        return std::nullopt;
    }

    const auto schemaVersion = optionalNumber<int> (root, "schemaVersion", 0);

    if (schemaVersion != supportedSchemaVersion)
    {
        error = file.getFileName() + " declares schema version " + juce::String (schemaVersion)
              + "; this build supports version " + juce::String (supportedSchemaVersion);
        return std::nullopt;
    }

    ModelManifest manifest;
    manifest.name = root->getProperty ("name").toString();

    manifest.modelSampleRate = requireNumber<int> (root, "modelSampleRate", error, "manifest");
    manifest.featureDim = requireNumber<int> (root, "featureDim", error, "manifest");
    manifest.latentDim = requireNumber<int> (root, "latentDim", error, "manifest");
    manifest.upsampleFactor = requireNumber<int> (root, "upsampleFactor", error, "manifest");
    manifest.numSpeakers = optionalNumber<int> (root, "numSpeakers", 1);
    manifest.speakerId = optionalNumber<int> (root, "speakerId", 0);

    const auto* graphs = requireObject (*root, "graphs", error);

    std::vector<std::string> contentInputs;
    std::vector<std::string> pitchInputs;

    readGraph (graphs, "contentEncoder", manifest.contentEncoderFile, contentInputs,
               manifest.contentEncoderOutput, error);
    readGraph (graphs, "pitchEstimator", manifest.pitchEstimatorFile, pitchInputs,
               manifest.pitchEstimatorOutput, error);
    // Exports made before the graph was called by its own name still say "vocoder", which is
    // only the last stage of it.
    juce::String legacyName;

    if (! readGraph (graphs, "synthesizer", manifest.synthesizerFile, manifest.synthesizerInputs,
                     manifest.synthesizerOutput, legacyName))
        readGraph (graphs, "vocoder", manifest.synthesizerFile, manifest.synthesizerInputs,
                   manifest.synthesizerOutput, error);

    if (! contentInputs.empty())
        manifest.contentEncoderInput = contentInputs.front();

    if (! pitchInputs.empty())
        manifest.pitchEstimatorInput = pitchInputs.front();

    const auto* contentEncoder = requireObject (*root, "contentEncoder", error);
    manifest.contentSampleRate = requireNumber<int> (contentEncoder, "sampleRate", error, "contentEncoder");
    manifest.contentFrameRate = requireNumber<int> (contentEncoder, "frameRate", error, "contentEncoder");
    manifest.contentMinimumNumSamples = optionalNumber<int> (contentEncoder, "minimumNumSamples", 400);

    const auto* frontEnd = requireObject (*root, "pitchFrontEnd", error);
    manifest.pitchSampleRate = requireNumber<int> (frontEnd, "sampleRate", error, "pitchFrontEnd");
    manifest.melFilterBankFile = frontEnd != nullptr ? frontEnd->getProperty ("filterBankFile").toString()
                                                     : juce::String();

    auto& mel = manifest.melConfiguration;
    mel.fftSize = requireNumber<int> (frontEnd, "fftSize", error, "pitchFrontEnd");
    mel.windowSize = optionalNumber<int> (frontEnd, "windowSize", mel.fftSize);
    mel.hopSizeInSamples = requireNumber<int> (frontEnd, "hopSizeInSamples", error, "pitchFrontEnd");
    mel.numMelBins = requireNumber<int> (frontEnd, "numMelBins", error, "pitchFrontEnd");
    mel.numBins = optionalNumber<int> (frontEnd, "numBins", mel.fftSize / 2 + 1);
    mel.magnitudeFloor = optionalNumber<float> (frontEnd, "magnitudeFloor", 1.0e-5f);
    mel.isCentred = optionalBool (frontEnd, "isCentred", true);

    const auto* decoder = requireObject (*root, "pitchDecoder", error);
    manifest.numPitchBins = requireNumber<int> (decoder, "numPitchBins", error, "pitchDecoder");
    manifest.centsOrigin = requireNumber<double> (decoder, "centsOrigin", error, "pitchDecoder");
    manifest.centsPerBin = requireNumber<double> (decoder, "centsPerBin", error, "pitchDecoder");
    manifest.centsReferenceHz = optionalNumber<double> (decoder, "centsReferenceHz", 10.0);
    manifest.localAverageRadius = optionalNumber<int> (decoder, "localAverageRadius", 4);
    manifest.salienceThreshold = optionalNumber<float> (decoder, "salienceThreshold", 0.03f);
    manifest.frameCountMultiple = optionalNumber<int> (decoder, "frameCountMultiple", 32);
    manifest.pitchMinimumHz = optionalNumber<double> (decoder, "minimumFrequencyHz", 50.0);
    manifest.pitchMaximumHz = optionalNumber<double> (decoder, "maximumFrequencyHz", 1100.0);
    manifest.numCoarsePitchBins = optionalNumber<int> (decoder, "numCoarsePitchBins", 255);

    if (const auto retrievalValue = root->getProperty ("retrieval"); auto* retrieval = retrievalValue.getDynamicObject())
    {
        manifest.retrievalFile = retrieval->getProperty ("file").toString();
        manifest.numRetrievalVectors = optionalNumber<int> (retrieval, "numVectors", 0);
        manifest.numNeighbours = optionalNumber<int> (retrieval, "numNeighbours", 8);
        manifest.graphDegree = optionalNumber<int> (retrieval, "graphDegree", 16);
        manifest.constructionCandidateListSize =
            optionalNumber<int> (retrieval, "constructionCandidateListSize", 200);
        manifest.searchCandidateListSize =
            optionalNumber<int> (retrieval, "searchCandidateListSize", 64);
    }

    if (const auto chunkingValue = root->getProperty ("chunking"); auto* chunking = chunkingValue.getDynamicObject())
    {
        manifest.contextPaddingSeconds = optionalNumber<double> (chunking, "contextPaddingSeconds", 3.0);
        manifest.splitSearchRadiusSeconds = optionalNumber<double> (chunking, "splitSearchRadiusSeconds", 10.0);
        manifest.chunkStrideSeconds = optionalNumber<double> (chunking, "chunkStrideSeconds", 60.0);
        manifest.maximumChunkSeconds = optionalNumber<double> (chunking, "maximumChunkSeconds", 65.0);
    }

    if (const auto filterValue = root->getProperty ("highPassFilter"); auto* filter = filterValue.getDynamicObject())
    {
        manifest.highPassPadLength = optionalNumber<int> (filter, "padLength", 18);

        if (const auto* sections = filter->getProperty ("sections").getArray())
        {
            for (const auto& sectionValue : *sections)
            {
                const auto* coefficients = sectionValue.getArray();

                if (coefficients == nullptr || coefficients->size() < 6)
                    continue;

                BiquadCoefficients section;
                section.b0 = static_cast<double> ((*coefficients)[0]);
                section.b1 = static_cast<double> ((*coefficients)[1]);
                section.b2 = static_cast<double> ((*coefficients)[2]);
                section.a0 = static_cast<double> ((*coefficients)[3]);
                section.a1 = static_cast<double> ((*coefficients)[4]);
                section.a2 = static_cast<double> ((*coefficients)[5]);

                manifest.highPassSections.push_back (section);
            }
        }
    }

    if (const auto defaultsValue = root->getProperty ("defaults"); auto* defaults = defaultsValue.getDynamicObject())
    {
        manifest.defaultPitchShiftSemitones = optionalNumber<float> (defaults, "pitchShiftSemitones", 0.0f);
        manifest.defaultRetrievalRatio = optionalNumber<float> (defaults, "retrievalRatio", 0.75f);
        manifest.defaultConsonantProtection = optionalNumber<float> (defaults, "consonantProtection", 0.33f);
        manifest.defaultEnvelopeFollowRatio = optionalNumber<float> (defaults, "envelopeFollowRatio", 0.0f);
        manifest.defaultLatentNoiseSeed = optionalNumber<int> (defaults, "latentNoiseSeed", 1);
    }

    if (error.isNotEmpty())
        return std::nullopt;

    if (! manifest.checkFrameRates (error))
        return std::nullopt;

    if (manifest.speakerId < 0 || manifest.speakerId >= manifest.numSpeakers)
    {
        error = "speaker id " + juce::String (manifest.speakerId) + " is outside the model's "
              + juce::String (manifest.numSpeakers) + " embeddings";
        return std::nullopt;
    }

    return manifest;
}
}
