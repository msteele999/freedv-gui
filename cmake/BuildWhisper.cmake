include(FetchContent)

# whisper.cpp is used for local decoded-speech transcription. Keep the
# dependency pinned so FreeDV builds remain reproducible.
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)

# FreeDV uses CPU inference. Avoid the pinned Metal backend's incompatibility
# with newer Apple SDKs; Accelerate remains available for CPU operations.
if(APPLE)
    set(GGML_METAL OFF CACHE BOOL "" FORCE)
    set(WHISPER_COREML OFF CACHE BOOL "" FORCE)
endif()

# GGML can enable IPO itself, independently of FreeDV's ENABLE_LTO option.
set(GGML_LTO OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG d09f61a708f3487afa956ff578e60eae5e7a233c
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(whisper)

# Isolate only the fetched dependency, including its nested GGML targets.
# FreeDV's standalone run-clang-tidy uses compile_commands.json, so disabling
# C/CXX_CLANG_TIDY alone is insufficient. Whisper enables database export in
# its own directory; override the target properties after it creates them.
# Likewise, keep FreeDV's IPO enabled but avoid the third-party GGML ARM LTO
# link failure. PGO flags and ordinary CPU optimization remain unchanged.
function(freedv_isolate_whisper_targets directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        set_target_properties(${target} PROPERTIES
            EXPORT_COMPILE_COMMANDS OFF
            C_CLANG_TIDY ""
            CXX_CLANG_TIDY ""
            OBJC_CLANG_TIDY ""
            OBJCXX_CLANG_TIDY ""
            INTERPROCEDURAL_OPTIMIZATION OFF
        )
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        freedv_isolate_whisper_targets("${subdirectory}")
    endforeach()
endfunction()

freedv_isolate_whisper_targets("${whisper_SOURCE_DIR}")

list(APPEND FREEDV_LINK_LIBS whisper)
