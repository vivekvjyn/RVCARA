#include "FeatureRetriever.h"

#include <hnswlib/hnswlib.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rvcara
{

FeatureRetriever::FeatureRetriever() = default;
FeatureRetriever::~FeatureRetriever() = default;

bool FeatureRetriever::prepare (const BinaryMatrix& codebook,
                                const Configuration& configuration,
                                const juce::File& cacheFile)
{
    index.reset();
    space.reset();
    error.clear();

    if (! codebook.isValid())
    {
        error = "retrieval codebook is not loaded";
        return false;
    }

    sourceCodebook = &codebook;
    config = configuration;
    featureDim = codebook.getNumColumns();
    numVectors = codebook.getNumRows();

    try
    {
        // L2 space, matching the euclidean distances FAISS reported during training. The
        // weighting downstream expects squared distances, which is what hnswlib returns.
        space = std::make_unique<hnswlib::L2Space> (static_cast<std::size_t> (featureDim));

        const auto maximumElements = static_cast<std::size_t> (numVectors);

        if (cacheFile.existsAsFile())
        {
            index = std::make_unique<hnswlib::HierarchicalNSW<float>> (
                space.get(), cacheFile.getFullPathName().toStdString(), false, maximumElements);

            if (index->cur_element_count != maximumElements)
            {
                // A cache built from a different codebook. Discard and rebuild rather
                // than search a graph whose indices point at the wrong vectors.
                index.reset();
                cacheFile.deleteFile();
            }
        }

        if (index == nullptr)
        {
            index = std::make_unique<hnswlib::HierarchicalNSW<float>> (
                space.get(),
                maximumElements,
                static_cast<std::size_t> (config.graphDegree),
                static_cast<std::size_t> (config.constructionCandidateListSize));

            for (int vectorIndex = 0; vectorIndex < numVectors; ++vectorIndex)
                index->addPoint (codebook.getRow (vectorIndex),
                                 static_cast<hnswlib::labeltype> (vectorIndex));

            if (cacheFile.getFullPathName().isNotEmpty())
            {
                cacheFile.getParentDirectory().createDirectory();
                index->saveIndex (cacheFile.getFullPathName().toStdString());
            }
        }

        index->setEf (static_cast<std::size_t> (std::max (config.searchCandidateListSize,
                                                          config.numNeighbours)));
        return true;
    }
    catch (const std::exception& exception)
    {
        error = "could not build the retrieval index: " + juce::String (exception.what());
        index.reset();
        space.reset();
        return false;
    }
}

void FeatureRetriever::retrieveFrame (const float* feature, float* destination) const
{
    auto neighbours = index->searchKnn (feature, static_cast<std::size_t> (config.numNeighbours));

    // searchKnn returns a max-heap of (squaredDistance, label), furthest first. Both the
    // distances and the labels are needed, so drain it into a local buffer.
    struct Neighbour
    {
        float weight;
        int vectorIndex;
    };

    std::vector<Neighbour> collected;
    collected.reserve (neighbours.size());

    double weightSum = 0.0;

    while (! neighbours.empty())
    {
        const auto [squaredDistance, label] = neighbours.top();
        neighbours.pop();

        // The reference weights by 1 / squaredDistance², which is an inverse fourth power
        // of euclidean distance: a very sharp falloff, so the nearest frame dominates.
        // An exact hit would divide by zero, so the distance is floored.
        const auto floored = std::max (static_cast<double> (squaredDistance),
                                       static_cast<double> (std::numeric_limits<float>::min()));
        const auto weight = 1.0 / (floored * floored);

        collected.push_back ({ static_cast<float> (weight), static_cast<int> (label) });
        weightSum += weight;
    }

    if (collected.empty() || weightSum <= 0.0)
    {
        std::copy (feature, feature + featureDim, destination);
        return;
    }

    std::fill (destination, destination + featureDim, 0.0f);

    const auto normalisation = static_cast<float> (1.0 / weightSum);

    for (const auto& neighbour : collected)
    {
        const auto* vector = sourceCodebook->getRow (neighbour.vectorIndex);
        const auto weight = neighbour.weight * normalisation;

        for (int dimension = 0; dimension < featureDim; ++dimension)
            destination[dimension] += weight * vector[dimension];
    }
}

void FeatureRetriever::blend (float* features, int numFrames, float retrievalRatio) const
{
    if (! isReady() || features == nullptr || numFrames <= 0 || retrievalRatio <= 0.0f)
        return;

    const auto ratio = std::min (retrievalRatio, 1.0f);
    const auto complement = 1.0f - ratio;

    std::vector<float> retrieved (static_cast<std::size_t> (featureDim));

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        auto* frame = features + static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (featureDim);

        retrieveFrame (frame, retrieved.data());

        for (int dimension = 0; dimension < featureDim; ++dimension)
            frame[dimension] = ratio * retrieved[static_cast<std::size_t> (dimension)]
                             + complement * frame[dimension];
    }
}

} // namespace rvcara
