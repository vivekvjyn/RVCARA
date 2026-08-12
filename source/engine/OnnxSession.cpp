#include "OnnxSession.h"

#include <utility>

namespace rvcara::engine
{

OnnxSession::~OnnxSession() = default;
OnnxSession::OnnxSession (OnnxSession&&) noexcept = default;
OnnxSession& OnnxSession::operator= (OnnxSession&&) noexcept = default;

Ort::Env& OnnxSession::getSharedEnvironment()
{
    // Function-local static: constructed on first use, after any static initialisation
    // the host has done, and destroyed at exit after the last session is gone.
    static Ort::Env environment { ORT_LOGGING_LEVEL_WARNING, "RVCARA" };
    return environment;
}

bool OnnxSession::load (const juce::File& file, int numThreads)
{
    session.reset();

    if (! file.existsAsFile())
    {
        error = "no such graph: " + file.getFullPathName();
        return false;
    }

    try
    {
        Ort::SessionOptions options;

        // Fuse and constant-fold everything available. The graphs are loaded once per
        // model and reused for every render, so optimisation time is paid once and the
        // vocoder — which dominates the conversion — benefits most.
        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (numThreads > 0)
            options.SetIntraOpNumThreads (numThreads);

        // The graphs have no branch parallelism worth exploiting; one operator at a
        // time, internally threaded, keeps scheduling overhead down.
        options.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);

       #if JUCE_WINDOWS
        session = std::make_unique<Ort::Session> (getSharedEnvironment(),
                                                  file.getFullPathName().toWideCharPointer(),
                                                  options);
       #else
        session = std::make_unique<Ort::Session> (getSharedEnvironment(),
                                                  file.getFullPathName().toRawUTF8(),
                                                  options);
       #endif

        error.clear();
        return true;
    }
    catch (const Ort::Exception& exception)
    {
        error = "could not load " + file.getFileName() + ": " + juce::String (exception.what());
        session.reset();
        return false;
    }
    catch (const std::exception& exception)
    {
        error = "could not load " + file.getFileName() + ": " + juce::String (exception.what());
        session.reset();
        return false;
    }
}

std::vector<std::string> OnnxSession::getInputNames() const
{
    std::vector<std::string> names;

    if (session == nullptr)
        return names;

    try
    {
        Ort::AllocatorWithDefaultOptions allocator;
        const auto numInputs = session->GetInputCount();

        names.reserve (numInputs);

        for (std::size_t inputIndex = 0; inputIndex < numInputs; ++inputIndex)
            names.emplace_back (session->GetInputNameAllocated (inputIndex, allocator).get());
    }
    catch (const Ort::Exception&)
    {
        names.clear();
    }

    return names;
}

std::vector<Ort::Value> OnnxSession::run (const std::vector<TensorView>& inputs,
                                          const std::vector<const char*>& outputNames) const
{
    if (session == nullptr)
    {
        error = "graph is not loaded";
        return {};
    }

    try
    {
        const auto memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<const char*> inputNames;
        std::vector<Ort::Value> inputValues;

        inputNames.reserve (inputs.size());
        inputValues.reserve (inputs.size());

        for (const auto& input : inputs)
        {
            std::int64_t numElements = 1;
            for (const auto dimension : input.shape)
                numElements *= dimension;

            inputNames.push_back (input.name);

            // The tensors alias the caller's buffers rather than copying: the content
            // features for a one-minute region are around 180 MB, and copying them into
            // the runtime on every chunk would dominate the transfer cost. The caller
            // must keep them alive until run() returns, which it does.
            if (input.floatData != nullptr)
            {
                inputValues.push_back (Ort::Value::CreateTensor<float> (
                    memoryInfo,
                    const_cast<float*> (input.floatData),
                    static_cast<std::size_t> (numElements),
                    input.shape.data(),
                    input.shape.size()));
            }
            else
            {
                inputValues.push_back (Ort::Value::CreateTensor<std::int64_t> (
                    memoryInfo,
                    const_cast<std::int64_t*> (input.intData),
                    static_cast<std::size_t> (numElements),
                    input.shape.data(),
                    input.shape.size()));
            }
        }

        auto outputs = session->Run (Ort::RunOptions { nullptr },
                                     inputNames.data(),
                                     inputValues.data(),
                                     inputValues.size(),
                                     outputNames.data(),
                                     outputNames.size());

        error.clear();
        return outputs;
    }
    catch (const Ort::Exception& exception)
    {
        error = juce::String (exception.what());
        return {};
    }
    catch (const std::exception& exception)
    {
        error = juce::String (exception.what());
        return {};
    }
}

} // namespace rvcara::engine
