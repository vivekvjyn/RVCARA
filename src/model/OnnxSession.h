#pragma once

#include <onnxruntime_cxx_api.h>

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rvcara
{
/** @brief One ONNX Runtime session, loaded from a file and run on borrowed buffers. */
class OnnxSession
{
public:
    /** @brief A tensor that aliases the caller's buffer for the duration of a run.

        An empty shape makes a scalar, which is how a graph takes a threshold or a radius.
    */
    struct TensorView
    {
        const char* name { nullptr };
        const float* floatData { nullptr };
        const std::int64_t* intData { nullptr };
        const std::uint8_t* boolData { nullptr };
        std::vector<std::int64_t> shape;

        static TensorView floats (const char* tensorName, const float* values, std::vector<std::int64_t> tensorShape)
        {
            return { tensorName, values, nullptr, nullptr, std::move (tensorShape) };
        }

        static TensorView integers (const char* tensorName, const std::int64_t* values, std::vector<std::int64_t> tensorShape)
        {
            return { tensorName, nullptr, values, nullptr, std::move (tensorShape) };
        }

        /** @brief One byte per element, which is how ONNX Runtime lays a bool tensor out. */
        static TensorView booleans (const char* tensorName, const std::uint8_t* values, std::vector<std::int64_t> tensorShape)
        {
            return { tensorName, nullptr, nullptr, values, std::move (tensorShape) };
        }
    };

    OnnxSession() = default;
    ~OnnxSession();

    OnnxSession (const OnnxSession&) = delete;
    OnnxSession& operator= (const OnnxSession&) = delete;
    OnnxSession (OnnxSession&&) noexcept;
    OnnxSession& operator= (OnnxSession&&) noexcept;

    bool load (const juce::File& file, int numThreads = 0);

    /** @brief Returns the session for a graph, loading it once and lending it to every caller.

        The content encoder and the pitch estimator are the same graphs whichever voice is
        loaded, and between them they are most of a voice's bytes, so changing voice must not
        reload them. Sessions are kept per resolved path, so two voices share one only when
        they name the same file, and a graph is dropped once the last voice using it goes.

        @param file        The graph to load.
        @param numThreads  Intra-op threads, honoured on the first load of a path.
        @param error       Set when the graph could not be loaded.
        @return The shared session, or nullptr when it could not be loaded.
    */
    [[nodiscard]] static std::shared_ptr<const OnnxSession> getShared (const juce::File& file,
                                                                       int numThreads,
                                                                       juce::String& error);

    [[nodiscard]] std::vector<Ort::Value> run (const std::vector<TensorView>& inputs,
                                               const std::vector<const char*>& outputNames) const;

    [[nodiscard]] bool isLoaded() const noexcept { return session != nullptr; }

    [[nodiscard]] const juce::String& getError() const noexcept { return error; }

    [[nodiscard]] std::vector<std::string> getInputNames() const;

private:
    static Ort::Env& getSharedEnvironment();

    std::unique_ptr<Ort::Session> session;
    mutable juce::String error;
};
}
