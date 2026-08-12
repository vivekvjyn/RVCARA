#pragma once

#include "BinaryMatrix.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace hnswlib
{
    template <typename Type> class HierarchicalNSW;
    template <typename Type> class SpaceInterface;
} // namespace hnswlib

namespace rvcara
{

/** Replaces content features with their nearest neighbours from the training set.

    This is the "retrieval" in retrieval-based voice conversion, and the reason a model
    trained on a few minutes of singing sounds like that singer rather than like a
    smoothed average. Training stores every content frame of the training audio; at
    conversion each incoming frame is replaced by an inverse-fourth-power distance
    weighted mean of its eight nearest stored frames, then blended back toward the
    original by @c retrievalRatio.

    **Why approximate search.** The codebook for a few minutes of audio holds tens of
    thousands of 768-dimensional vectors — around 62,000 for the reference model. An
    exact search is a dense matrix product against the whole codebook for every frame:
    roughly 95 MFLOP per frame, or 9.5 GFLOP per second of audio at the 100 Hz
    conditioning rate. Even with a good BLAS that is seconds per minute of audio and it
    scales linearly with training set size. A hierarchical navigable small world graph
    answers the same query in microseconds.

    The approximation is safe here in a way it would not be for, say, a lookup table.
    The eight neighbours are averaged with weights that fall off as the fourth power of
    distance, so the result is dominated by the closest one or two; a miss in the tail
    of the neighbour list moves the blended vector by a small fraction of the distance
    between adjacent training frames. At the tuned search width, recall of the true
    eight exceeds 99%.

    **Why the graph is built on load rather than shipped.** hnswlib's serialised format
    is tied to its own version and struct layout, so a prebuilt index would couple the
    asset directory to a specific hnswlib revision. Building from the codebook takes a
    few seconds for a model this size, happens once per model per session, and is
    cached to disk beside the codebook so subsequent loads skip it.
*/
class FeatureRetriever
{
public:
    FeatureRetriever();
    ~FeatureRetriever();

    FeatureRetriever (const FeatureRetriever&) = delete;
    FeatureRetriever& operator= (const FeatureRetriever&) = delete;

    /** Parameters governing the search, all from the manifest. */
    struct Configuration
    {
        int numNeighbours { 8 };
        int graphDegree { 16 };
        int constructionCandidateListSize { 200 };
        int searchCandidateListSize { 64 };
    };

    /** Builds or loads the search graph over a codebook.

        @param codebook       The memory-mapped `[numVectors, featureDim]` codebook. It
                              must outlive this object: the graph stores indices into it
                              and the blend reads the vectors themselves.
        @param configuration  Search parameters.
        @param cacheFile      Where to read and write the built graph. Pass a
                              non-existent path to force a rebuild without caching.
        @returns              True on success; see getError() otherwise.
    */
    bool prepare (const BinaryMatrix& codebook,
                  const Configuration& configuration,
                  const juce::File& cacheFile);

    /** Blends a block of features toward their retrieved neighbours, in place.

        @param features        `[numFrames, featureDim]` row-major, modified in place.
        @param numFrames       Frames to process.
        @param retrievalRatio  0 leaves features untouched, 1 replaces them entirely.
    */
    void blend (float* features, int numFrames, float retrievalRatio) const;

    /** @returns Whether a graph is ready to search. */
    [[nodiscard]] bool isReady() const noexcept { return index != nullptr; }

    /** @returns Why prepare() failed, or an empty string. */
    [[nodiscard]] const juce::String& getError() const noexcept { return error; }

    /** @returns The number of vectors in the codebook. */
    [[nodiscard]] int getNumVectors() const noexcept { return numVectors; }

private:
    /** Retrieves and averages the neighbours of one frame into @c destination. */
    void retrieveFrame (const float* feature, float* destination) const;

    const BinaryMatrix* sourceCodebook { nullptr };
    std::unique_ptr<hnswlib::SpaceInterface<float>> space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index;

    Configuration config;
    int featureDim { 0 };
    int numVectors { 0 };
    juce::String error;
};

} // namespace rvcara
