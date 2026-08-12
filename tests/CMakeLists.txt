# Unit tests for the conversion engine.
#
# These link rvcara_engine directly and never load a plugin, so they run in a second
# and can be run from CI on a machine with no audio device and no voice model
# installed. The tests that need a real model are guarded by the [model] tag and skip
# themselves when RVCARA_MODEL_PATH is unset.

add_executable(rvcara_tests
    BinaryMatrixTests.cpp
    ConversionSettingsTests.cpp
    MelSpectrogramTests.cpp
    PitchConversionsTests.cpp
    SincResamplerTests.cpp
    ZeroPhaseFilterTests.cpp)

target_link_libraries(rvcara_tests
    PRIVATE
        rvcara_engine
        rvcara_warnings
        Catch2::Catch2WithMain)

target_compile_definitions(rvcara_tests PRIVATE
    RVCARA_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# ONNX Runtime is a shared library, and the test binary loads it at start-up even for
# the tests that never touch a graph.
rvcara_copy_onnxruntime_beside(rvcara_tests)

include(Catch)
catch_discover_tests(rvcara_tests)
