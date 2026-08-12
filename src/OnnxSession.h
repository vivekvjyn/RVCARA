#pragma once

#include <onnxruntime_cxx_api.h>

#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rvcara
{

/** One loaded ONNX graph, with tensors bound by name.

    A thin wrapper, for three specific reasons rather than for its own sake:

    - **Names come from the manifest, never from string literals in C++.** Binding by
      name means a differently exported graph — including a third-party RVC export
      whose tensors are called `phone` and `pitchf` — loads by editing a JSON file
      instead of recompiling. `run()` therefore takes names as arguments.
    - **ONNX Runtime's C++ API reports every error by throwing `Ort::Exception`.** A
      throw crossing into a host's audio thread or its plugin scanner is not
      acceptable, so failures are converted to a return value and an error string at
      this boundary and nowhere deeper.
    - The environment must outlive every session, which is easy to get wrong with
      free functions and is enforced here by a shared static.

    Sessions are not thread-safe for concurrent `run()` calls at these settings; each
    conversion worker owns its own model instance.
*/
class OnnxSession
{
public:
    /** Describes one tensor being fed to or read from a graph. */
    struct TensorView
    {
        const char* name { nullptr };
        const float* floatData { nullptr };
        const std::int64_t* intData { nullptr };
        std::vector<std::int64_t> shape;

        /** Binds a float32 tensor. */
        static TensorView floats (const char* tensorName, const float* values, std::vector<std::int64_t> tensorShape)
        {
            return { tensorName, values, nullptr, std::move (tensorShape) };
        }

        /** Binds an int64 tensor. */
        static TensorView integers (const char* tensorName, const std::int64_t* values, std::vector<std::int64_t> tensorShape)
        {
            return { tensorName, nullptr, values, std::move (tensorShape) };
        }
    };

    OnnxSession() = default;
    ~OnnxSession();

    OnnxSession (const OnnxSession&) = delete;
    OnnxSession& operator= (const OnnxSession&) = delete;
    OnnxSession (OnnxSession&&) noexcept;
    OnnxSession& operator= (OnnxSession&&) noexcept;

    /** Loads a graph.

        @param file        The `.onnx` file.
        @param numThreads  Intra-operator threads; zero lets ONNX Runtime choose,
                           which is one per physical core.
        @returns           True on success; on failure see getError().
    */
    bool load (const juce::File& file, int numThreads = 0);

    /** Runs the graph once.

        @param inputs       Tensors to feed, named as the graph declares them.
        @param outputNames  Outputs to fetch.
        @returns            The output tensors, empty on failure.
    */
    [[nodiscard]] std::vector<Ort::Value> run (const std::vector<TensorView>& inputs,
                                               const std::vector<const char*>& outputNames) const;

    /** @returns Whether a graph is loaded. */
    [[nodiscard]] bool isLoaded() const noexcept { return session != nullptr; }

    /** @returns The most recent error, or an empty string. */
    [[nodiscard]] const juce::String& getError() const noexcept { return error; }

    /** @returns The graph's declared input names, for diagnostics and manifest checks. */
    [[nodiscard]] std::vector<std::string> getInputNames() const;

private:
    /** @returns The process-wide ONNX Runtime environment, created on first use.

        One environment is shared by every session: it owns the logging sink and the
        thread pools, and constructing more than one wastes both.
    */
    static Ort::Env& getSharedEnvironment();

    std::unique_ptr<Ort::Session> session;
    mutable juce::String error;
};

} // namespace rvcara
