include(FetchContent)

# whisper.cpp is used for local decoded-speech transcription. Keep the
# dependency pinned so FreeDV builds remain reproducible.
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG d09f61a708f3487afa956ff578e60eae5e7a233c
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(whisper)

list(APPEND FREEDV_LINK_LIBS whisper)
