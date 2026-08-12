cmake_minimum_required(VERSION 3.24)

project(RVCARA
    VERSION 0.1.0
    DESCRIPTION "Retrieval-based voice conversion as an ARA plugin"
    HOMEPAGE_URL "https://github.com/vivekvjyn/RVCARA"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code throughout: every plugin format is a shared library, and
# the engine is linked into all of them.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(RVCARA_BUILD_TESTS "Build the unit tests" ON)

# ==================================================================================
# Dependencies
#
# All four are submodules under libs/. ONNX Runtime is the one exception and is
# fetched as a pinned prebuilt release; see cmake/OnnxRuntime.cmake for why.
# ==================================================================================

foreach(submodule juce ara-sdk hnswlib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/.git"
       AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/CMakeLists.txt"
       AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/README.md")
        message(FATAL_ERROR
            "libs/${submodule} is empty. Run:\n"
            "    git submodule update --init --recursive")
    endif()
endforeach()

add_subdirectory(libs/juce)

# juce_set_ara_sdk_path is defined by JUCE's own CMake utilities, so it can only be
# called after JUCE has been added — but it must still precede any target that depends
# on the ARA SDK, which is why it sits here rather than next to the plugin. JUCE
# compiles the ARA library's sources into juce_audio_processors itself, so an include
# path is all it needs from us.
juce_set_ara_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/libs/ara-sdk")

include(OnnxRuntime)
rvcara_add_onnxruntime()

# hnswlib is header-only, and its own CMakeLists builds example programs and tests
# we have no use for, so it is consumed as an interface target rather than added as
# a subdirectory.
add_library(rvcara_hnswlib INTERFACE)
target_include_directories(rvcara_hnswlib SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/hnswlib")
add_library(rvcara::hnswlib ALIAS rvcara_hnswlib)

# ==================================================================================
# Warnings
#
# JUCE distributes its modules as interface sources, which means the module .cpp and .c
# files are compiled *as part of* whichever target links them. Target-level warning
# flags therefore apply to JUCE's code as well as ours, and JUCE is not warning-clean
# under a strict set — its audio conversion headers promote float to double by design,
# and the vendored FLAC and Ogg sources are C, which rejects the C++-only flags outright.
#
# So there are two tiers. `rvcara::warnings` is the set that is meaningful without
# drowning in third-party noise, restricted to C++ so the vendored C compiles.
# `rvcara_apply_strict_warnings` adds the flags we want on our own DSP — chiefly the
# silent float-to-double promotions that matter in audio code — to named source files
# only, where JUCE's headers are included but JUCE's implementations are not compiled.
# ==================================================================================

add_library(rvcara_warnings INTERFACE)
add_library(rvcara::warnings ALIAS rvcara_warnings)

if(MSVC)
    target_compile_options(rvcara_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
else()
    target_compile_options(rvcara_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic
            -Wshadow                 # a shadowed sampleRate is a real bug here
            -Wnon-virtual-dtor
            -Wcast-align
            -Woverloaded-virtual>)   # catches a mistyped ARA override
endif()

function(rvcara_apply_strict_warnings)
    if(MSVC)
        return()
    endif()

    foreach(sourceFile ${ARGN})
        set_property(SOURCE "${sourceFile}" APPEND PROPERTY COMPILE_OPTIONS
            -Wdouble-promotion       # float32 audio silently widened to double
            -Wfloat-conversion
            -Wold-style-cast)
    endforeach()
endfunction()

add_subdirectory(source)

if(RVCARA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(libs/catch2)
    add_subdirectory(tests)
endif()
