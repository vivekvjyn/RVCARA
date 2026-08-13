cmake_minimum_required(VERSION 3.24)

project(RVCARA
    VERSION 0.1.0
    DESCRIPTION "Retrieval-based voice conversion as an ARA plug-in"
    HOMEPAGE_URL "https://github.com/vivekvjyn/RVCARA"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(RVCARA_BUILD_TESTS "Build the unit tests" ON)

# ==================================================================================
# ONNX Runtime
#
# Built from libs/onnxruntime, the pinned submodule, so that every line of code this plug-in
# runs is a commit this repository names. Only the CPU execution provider is wanted, and
# only the shared library and its public headers are installed.
#
# What this costs, so nobody is surprised by it:
#
# - The first build takes 25 to 45 minutes. It is built once into build/onnxruntime-install
#   and reused after that; a rebuild of the plug-in does not touch it.
# - Python 3 is a *build tool* for it. cmake/onnxruntime.cmake generates the export symbol
#   list by running tools/ci_build/gen_def.py, so a shared-library build cannot proceed
#   without an interpreter. Nothing in RVCARA needs Python, and nothing here runs at
#   plug-in load; this is ONNX Runtime's build requirement, checked for up front so the
#   failure is a sentence rather than a stack of CMake errors 30 seconds in.
# - It configures with the network open: ONNX Runtime fetches its own dependency tree —
#   abseil, protobuf, flatbuffers, re2, eigen, cpuinfo and the rest listed in
#   libs/onnxruntime/cmake/deps.txt — rather than vendoring it. The submodule pins ONNX
#   Runtime; deps.txt pins those.
#
# Three arguments below are not optional, all three because a pinned release is older than the
# toolchain compiling it:
#
# - --compile-no-warning-as-error, because ONNX Runtime sets COMPILE_WARNING_AS_ERROR on every
#   one of its targets and a newer compiler always finds something new to say. GCC 16 rejects
#   1.23.2 outright over -Wsfinae-incomplete and -Wdangling-pointer, in code that is fine.
# - CMAKE_POLICY_VERSION_MINIMUM, because CMake 4 refuses the pre-3.5 minimums that several of
#   those fetched dependencies still declare.
# - -include cstdint on GCC 15 and newer, for the one place the sources genuinely are wrong.
#
# Set RVCARA_ONNXRUNTIME_ROOT to an existing installation to skip all of it — the escape
# hatch for CI, for a distribution package, and for anyone who would rather use the binaries
# Microsoft tests.
# ==================================================================================

set(RVCARA_ONNXRUNTIME_ROOT "" CACHE PATH
    "Existing ONNX Runtime installation to use instead of building libs/onnxruntime")

if(RVCARA_ONNXRUNTIME_ROOT)
    set(onnxruntimeRoot "${RVCARA_ONNXRUNTIME_ROOT}")
    message(STATUS "ONNX Runtime: using ${onnxruntimeRoot}")

    # An install tree puts the public headers in include/onnxruntime; the published archives put
    # them straight in include. Both are accepted, since either may be handed to us here.
    if(EXISTS "${onnxruntimeRoot}/include/onnxruntime/onnxruntime_cxx_api.h")
        set(onnxruntimeIncludeDir "${onnxruntimeRoot}/include/onnxruntime")
    elseif(EXISTS "${onnxruntimeRoot}/include/onnxruntime_cxx_api.h")
        set(onnxruntimeIncludeDir "${onnxruntimeRoot}/include")
    else()
        message(FATAL_ERROR "No onnxruntime_cxx_api.h under ${onnxruntimeRoot}/include")
    endif()
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/onnxruntime/cmake/CMakeLists.txt")
        message(FATAL_ERROR
            "libs/onnxruntime is empty. Run:\n"
            "    git submodule update --init --depth 1 libs/onnxruntime\n"
            "    git -C libs/onnxruntime submodule update --init --depth 1 cmake/external/onnx")
    endif()

    # ONNX Runtime's own submodule, its copy of the ONNX protobuf schema. Its build stops with
    # a less helpful message than this one if it is missing.
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/onnxruntime/cmake/external/onnx/CMakeLists.txt")
        message(FATAL_ERROR
            "ONNX Runtime's schema submodule is not checked out. Run:\n"
            "    git -C libs/onnxruntime submodule update --init --depth 1 cmake/external/onnx")
    endif()

    find_package(Python3 3.10 COMPONENTS Interpreter)

    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR
            "Building ONNX Runtime needs a Python 3.10+ interpreter for its symbol-list "
            "generator (tools/ci_build/gen_def.py). Install one, or point "
            "RVCARA_ONNXRUNTIME_ROOT at a prebuilt installation.")
    endif()

    include(ExternalProject)

    set(onnxruntimeRoot "${CMAKE_BINARY_DIR}/onnxruntime-install")
    set(onnxruntimeCxxFlags "")

    # 1.23.2 predates GCC 15, from which libstdc++ stopped pulling <cstdint> in transitively;
    # core/common/semver.h uses uint32_t without including it. Forcing the header in ahead of
    # every translation unit fixes it without patching a pinned submodule.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
        set(onnxruntimeCxxFlags "-include cstdint")
    endif()

    if(WIN32)
        set(onnxruntimeBuiltLibrary "${onnxruntimeRoot}/bin/onnxruntime.dll")
    elseif(APPLE)
        set(onnxruntimeBuiltLibrary "${onnxruntimeRoot}/lib/libonnxruntime.dylib")
    else()
        set(onnxruntimeBuiltLibrary "${onnxruntimeRoot}/lib/libonnxruntime.so")
    endif()

    message(STATUS "ONNX Runtime: building libs/onnxruntime into ${onnxruntimeRoot}")

    ExternalProject_Add(rvcara_onnxruntime_build
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/onnxruntime/cmake"
        BINARY_DIR "${CMAKE_BINARY_DIR}/onnxruntime-build"
        INSTALL_DIR "${onnxruntimeRoot}"
        CMAKE_ARGS
            --compile-no-warning-as-error
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_INSTALL_PREFIX=${onnxruntimeRoot}"
            "-DCMAKE_CXX_FLAGS=${onnxruntimeCxxFlags}"
            -DCMAKE_INSTALL_LIBDIR=lib
            # Release whatever this project is built as: ONNX Runtime in Debug is an order of
            # magnitude slower, and nobody steps into it from here.
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            -Donnxruntime_BUILD_SHARED_LIB=ON
            -Donnxruntime_BUILD_UNIT_TESTS=OFF
            -Donnxruntime_BUILD_BENCHMARKS=OFF
            -Donnxruntime_ENABLE_PYTHON=OFF
        BUILD_BYPRODUCTS "${onnxruntimeBuiltLibrary}"
        USES_TERMINAL_CONFIGURE TRUE
        USES_TERMINAL_BUILD TRUE
        USES_TERMINAL_INSTALL TRUE)

    # `install(TARGETS ... PUBLIC_HEADER)` in cmake/onnxruntime.cmake puts them here. Stated
    # rather than probed, because on a first build nothing is installed yet and a probe would
    # answer differently before and after.
    set(onnxruntimeIncludeDir "${onnxruntimeRoot}/include/onnxruntime")
endif()

if(WIN32)
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/onnxruntime.lib")
    set(onnxruntimeLibrary "${onnxruntimeRoot}/bin/onnxruntime.dll")
elseif(APPLE)
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/libonnxruntime.dylib")
    set(onnxruntimeLibrary "${onnxruntimeImplib}")
else()
    set(onnxruntimeImplib "${onnxruntimeRoot}/lib/libonnxruntime.so")
    set(onnxruntimeLibrary "${onnxruntimeImplib}")
endif()

# An imported target rejects an include directory that does not exist yet, and when the library
# is built by this project the install tree appears only part way through the build.
file(MAKE_DIRECTORY "${onnxruntimeIncludeDir}")

add_library(rvcara_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(rvcara_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${onnxruntimeLibrary}"
    IMPORTED_IMPLIB "${onnxruntimeImplib}"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntimeIncludeDir}")

set(RVCARA_ONNXRUNTIME_LIBRARY_DIR "${onnxruntimeRoot}/lib")

#[[ Makes `target` wait for ONNX Runtime, when this project is the one building it.

    An imported target carries no build order, so anything that compiles against those headers
    has to be told to wait for them itself.
]]
function(rvcara_wait_for_onnxruntime target)
    if(TARGET rvcara_onnxruntime_build)
        add_dependencies(${target} rvcara_onnxruntime_build)
    endif()
endfunction()

# A plug-in cannot rely on the host's library search path, so every artefact gets its own copy.
#
# On Linux and macOS that means the whole soname chain — the loader asks for libonnxruntime.so.1,
# not the bare name — which is a set of files that cannot be listed at configure time, because on
# a first build ONNX Runtime has not been built yet. Hence a generated script that globs at build
# time. Copying the library directory wholesale would be simpler but would also deposit the
# install tree's cmake/ and pkgconfig/ directories inside every artefact.
set(rvcaraCopyOnnxRuntimeScript "${CMAKE_BINARY_DIR}/rvcara_copy_onnxruntime.cmake")

file(WRITE "${rvcaraCopyOnnxRuntimeScript}" "\
file(GLOB libraries \"\${libraryDir}/libonnxruntime*\")

foreach(library \${libraries})
    if(NOT IS_DIRECTORY \"\${library}\")
        file(COPY \"\${library}\" DESTINATION \"\${destination}\" FOLLOW_SYMLINK_CHAIN)
    endif()
endforeach()
")

function(rvcara_copy_onnxruntime_beside target)
    rvcara_wait_for_onnxruntime(${target})

    # Make the loader look beside the binary. Without this the copy is decoration: CMake records
    # the directory it linked against, an absolute path into this build tree, so the artefact
    # resolves libonnxruntime.so.1 only on the machine that built it and only for as long as the
    # build directory survives. BUILD_WITH_INSTALL_RPATH replaces that path rather than adding to
    # it. Ignored on Windows, which has no runpath — a DLL beside the module is already found.
    if(APPLE)
        set(loaderRelativePath "@loader_path")
    else()
        set(loaderRelativePath "$ORIGIN")
    endif()

    set_target_properties(${target} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${loaderRelativePath}")

    if(WIN32)
        add_custom_command(TARGET ${target} PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${onnxruntimeLibrary}" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
    else()
        add_custom_command(TARGET ${target} PRE_LINK
            COMMAND ${CMAKE_COMMAND}
                    "-DlibraryDir=${RVCARA_ONNXRUNTIME_LIBRARY_DIR}"
                    "-Ddestination=$<TARGET_FILE_DIR:${target}>"
                    -P "${rvcaraCopyOnnxRuntimeScript}" VERBATIM)
    endif()
endfunction()

# ==================================================================================
# Submodules
# ==================================================================================

foreach(submodule JUCE ARA_SDK googletest hnswlib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/README.md"
       AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/CMakeLists.txt")
        message(FATAL_ERROR "libs/${submodule} is empty. See the build steps in README.md.")
    endif()
endforeach()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/ARA_SDK/ARA_Library/PlugIn/ARAPlug.h")
    message(FATAL_ERROR
        "The ARA SDK's own submodules are not checked out. Run:\n"
        "    git -C libs/ARA_SDK submodule update --init --depth 1 ARA_API ARA_Library")
endif()

add_subdirectory(libs/JUCE)

# Defined by JUCE's CMake utilities, so it can only be called after JUCE is added — but it
# must still precede any target depending on the ARA SDK. JUCE compiles the ARA library's
# sources itself, so an include path is all it needs from us.
juce_set_ara_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/libs/ARA_SDK")

# hnswlib is header only, and its own CMakeLists builds examples and tests we have no use
# for, so it is consumed as an interface target rather than added as a subdirectory.
add_library(rvcara_hnswlib INTERFACE)
target_include_directories(rvcara_hnswlib SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libs/hnswlib")

# ==================================================================================
# Warnings
#
# Two tiers, and the line between them is about ownership rather than taste.
#
# The target-wide tier is everything JUCE's own code passes cleanly. It is restricted to C++
# because JUCE ships its modules as interface sources — the module .cpp and .c files compile as
# part of whichever target links them, so target-level flags reach JUCE's code, and the
# vendored FLAC and Ogg are C, which rejects the C++-only flags.
#
# The strict tier catches what actually costs something here: a float silently widened to
# double, an implicit narrowing, a C-style cast hiding a reinterpret. A warning flag judges
# every header a translation unit includes, and JUCE's headers contain 6,166 old-style casts
# and 116 float promotions of their own. GCC offers no way to exempt them that does not also
# stop it looking at our code, so nothing is silenced: the strict tier is applied to exactly
# those translation units that include no third-party header, and the DSP core is kept free of
# framework headers so that it can carry it. Units that must include JUCE — the plug-in, the
# ARA layer, and the engine glue that reports errors as juce::String — get the target-wide tier
# only. That is a real loss of checking on those files, and the honest price of not reaching
# for a silencer. To check one of them anyway, compile it with the strict flags appended and
# read the diagnostics whose path is under src/:
#
#   c++ $(rvcara flags from build/compile_commands.json) -Wconversion -Wold-style-cast \
#       -c src/ConversionEngine.cpp -o /dev/null 2>&1 | grep rvcplugin/src/
# ==================================================================================

add_library(rvcara_warnings INTERFACE)

if(MSVC)
    target_compile_options(rvcara_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
else()
    target_compile_options(rvcara_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic
            -Wshadow                # a shadowed sampleRate is a real bug here
            -Wnon-virtual-dtor
            -Wcast-align
            -Woverloaded-virtual>)  # catches a mistyped ARA override
endif()

#[[ Applies the strict tier to third-party-free translation units.

    Source properties are directory-scoped, so this has to be called from the CMakeLists.txt
    that compiles the files, not from here on behalf of a subdirectory.
]]
function(rvcara_apply_strict_warnings)
    if(MSVC)
        return()
    endif()

    foreach(sourceFile ${ARGN})
        set_property(SOURCE "${sourceFile}" APPEND PROPERTY COMPILE_OPTIONS
            -Wdouble-promotion      # float32 audio silently widened to double
            -Wfloat-conversion
            -Wconversion            # and every other implicit narrowing
            -Wsign-conversion       # a negative index into a std::size_t
            -Wold-style-cast
            -Wuseless-cast
            -Wcast-qual
            -Wextra-semi)
    endforeach()
endfunction()

# ==================================================================================
# The conversion engine
#
# Built as a static library separate from the plug-in, which is what makes it testable: it
# knows nothing about ARA, plug-in formats or the message thread, and the tests link it
# directly without instantiating a plug-in.
# ==================================================================================

set(RVCARA_ENGINE_SOURCES
    src/common/BinaryMatrix.cpp
    src/dsp/MelSpectrogram.cpp
    src/dsp/SincResampler.cpp
    src/dsp/ZeroPhaseFilter.cpp
    src/model/ConversionEngine.cpp
    src/model/FeatureRetriever.cpp
    src/model/ModelManifest.cpp
    src/model/OnnxSession.cpp
    src/model/PitchEstimator.cpp
    src/model/VoiceLoader.cpp
    src/model/VoiceModel.cpp
    src/model/VoiceModelLibrary.cpp)

add_library(rvcara_engine STATIC ${RVCARA_ENGINE_SOURCES})
target_include_directories(rvcara_engine PUBLIC src)

# The first thing this compiles is an ONNX Runtime header, which may not exist yet.
rvcara_wait_for_onnxruntime(rvcara_engine)

target_link_libraries(rvcara_engine
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_dsp
        rvcara_onnxruntime
    PRIVATE
        rvcara_hnswlib
        rvcara_warnings)

# JUCE_STANDALONE_APPLICATION is deliberately absent: the plug-in client defines it per
# format target, and setting it here would redefine it for the standalone build.
target_compile_definitions(rvcara_engine PUBLIC
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

# Where this working copy keeps its exported voices, so a build finds them without an install
# step. It has to be an absolute path decided here: the plug-in binary ends up in the host's
# plug-in folder, from which the repository is not reachable by any relative path.
target_compile_definitions(rvcara_engine PRIVATE
    RVCARA_DEVELOPMENT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/res/models")

# hnswlib's headers trip -Wall on unused parameters and sign comparisons in ways that are
# not ours to fix, and they are included by one translation unit.
if(NOT MSVC)
    set_property(SOURCE src/model/FeatureRetriever.cpp APPEND PROPERTY COMPILE_OPTIONS
        -Wno-unused-parameter -Wno-sign-compare)
endif()

# ==================================================================================
# The plug-in
#
# IS_ARA_EFFECT is what turns this from an ordinary VST3/AU into one a compatible host can
# load with timeline access. It defines JucePlugin_Enable_ARA, enables the ARA code paths in
# JUCE's VST3 and AU wrappers, and requires createARAFactory() alongside createPluginFilter().
#
# The two ARA identifiers are part of the compatibility surface. ARA_FACTORY_ID must change
# when the analysis or playback capabilities change; ARA_DOCUMENT_ARCHIVE_ID must change when
# the archive format changes incompatibly. The escaped quotes are required — JUCE passes the
# value straight into a preprocessor define, so it has to carry its own literal quotes.
#
# ARA_ANALYSIS_TYPES is left at its default of none: pitch is estimated but never exposed to
# the host as readable content, and claiming a capability the document controller does not
# implement would have hosts querying for content readers that do not exist.
# ==================================================================================

set(RVCARA_FORMATS VST3 LV2)

if(APPLE)
    list(APPEND RVCARA_FORMATS AU)
endif()

juce_add_plugin(RVCARA
    PLUGIN_MANUFACTURER_CODE Pinn
    PLUGIN_CODE Rvca
    MANUFACTURER_NAME "Pitch Innovations"
    MANUFACTURER_WEBSITE "https://github.com/vivekvjyn/RVCARA"
    COMPANY_NAME "Pitch Innovations"
    BUNDLE_ID "com.pitchinnovations.rvcara"
    PRODUCT_NAME "RVCARA"
    DESCRIPTION "Retrieval-based voice conversion as an ARA plug-in"

    FORMATS ${RVCARA_FORMATS}
    LV2URI "https://github.com/vivekvjyn/RVCARA"
    IS_ARA_EFFECT TRUE
    ARA_FACTORY_ID "\"com.pitchinnovations.rvcara.arafactory.0.1.0\""
    ARA_DOCUMENT_ARCHIVE_ID "\"com.pitchinnovations.rvcara.aradocumentarchive.1\""

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE)

set(RVCARA_PLUGIN_SOURCES
    src/Editor.cpp
    src/Processor.cpp
    src/ara/ConversionModification.cpp
    src/ara/DocumentController.cpp
    src/ara/PlaybackRenderer.cpp
    src/insert/InsertConverter.cpp
    src/ui/PanelLookAndFeel.cpp
    src/ui/PitchCurveView.cpp)

target_sources(RVCARA PRIVATE ${RVCARA_PLUGIN_SOURCES})
target_include_directories(RVCARA PRIVATE src)

target_compile_definitions(RVCARA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(RVCARA
    PRIVATE
        rvcara_engine
        rvcara_warnings
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# The resampler and the filter are the whole of the DSP that owes nothing to a framework —
# verified with `c++ -MM`, which lists no header from libs/ for either — so they carry the
# strict tier. MelSpectrogram cannot join them while it uses juce::dsp::FFT, and the pipeline
# and plug-in files report errors as juce::String.
rvcara_apply_strict_warnings(
    src/dsp/SincResampler.cpp
    src/dsp/ZeroPhaseFilter.cpp)

foreach(format ${RVCARA_FORMATS})
    if(TARGET RVCARA_${format})
        rvcara_copy_onnxruntime_beside(RVCARA_${format})
    endif()
endforeach()

find_package(Doxygen)

if(DOXYGEN_FOUND)
    add_custom_target(rvcara_docs
        COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
        COMMENT "Generating API documentation"
        VERBATIM)
endif()

if(RVCARA_BUILD_TESTS)
    enable_testing()
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(libs/googletest)
    add_subdirectory(tests)
endif()
