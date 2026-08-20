#include "model/OnnxSession.h"

#include <map>
#include <utility>

namespace rvcara
{
OnnxSession::~OnnxSession() = default;
OnnxSession::OnnxSession (OnnxSession&&) noexcept = default;
OnnxSession& OnnxSession::operator= (OnnxSession&&) noexcept = default;

Ort::Env& OnnxSession::getSharedEnvironment()
{
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

        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

        options.SetIntraOpNumThreads (numThreads > 0 ? numThreads
                                                     : juce::SystemStats::getNumPhysicalCpus());

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

std::shared_ptr<const OnnxSession> OnnxSession::getShared (const juce::File& file,
                                                           int numThreads,
                                                           juce::String& error)
{
    static juce::CriticalSection loadLock;
    static std::map<juce::String, std::weak_ptr<const OnnxSession>> loaded;

    const auto path = file.getFullPathName();

    const juce::ScopedLock lock { loadLock };

    if (const auto found = loaded.find (path); found != loaded.end())
    {
        if (auto existing = found->second.lock())
            return existing;

        loaded.erase (found);
    }

    auto session = std::make_shared<OnnxSession>();

    if (! session->load (file, numThreads))
    {
        error = session->getError();
        return nullptr;
    }

    loaded[path] = session;
    return session;
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

            if (input.floatData != nullptr)
            {
                inputValues.push_back (Ort::Value::CreateTensor<float> (
                    memoryInfo,
                    const_cast<float*> (input.floatData),
                    static_cast<std::size_t> (numElements),
                    input.shape.data(),
                    input.shape.size()));
            }
            else if (input.intData != nullptr)
            {
                inputValues.push_back (Ort::Value::CreateTensor<std::int64_t> (
                    memoryInfo,
                    const_cast<std::int64_t*> (input.intData),
                    static_cast<std::size_t> (numElements),
                    input.shape.data(),
                    input.shape.size()));
            }
            else
            {
                inputValues.push_back (Ort::Value::CreateTensor (
                    memoryInfo,
                    const_cast<std::uint8_t*> (input.boolData),
                    static_cast<std::size_t> (numElements),
                    input.shape.data(),
                    input.shape.size(),
                    ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL));
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
}
