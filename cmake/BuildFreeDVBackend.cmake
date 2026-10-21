include(FetchContent)

FetchContent_Declare(
    freedv_backend
    GIT_REPOSITORY https://github.com/freedv/freedv-backend
    # Retain the validated enhanced-client revision and OpenSSL workaround.
    GIT_TAG e9d58299926fe438cba5ec5cb5a6870028c718ac
    PATCH_COMMAND
        ${CMAKE_COMMAND}
        -DBACKEND_SOURCE=<SOURCE_DIR>/src/util/TcpConnectionHandler.cpp
        -P ${CMAKE_CURRENT_LIST_DIR}/PatchBackendOpenSSL.cmake
)

FetchContent_MakeAvailable(freedv_backend)

# rade_BINARY_DIR is set in freedv_backend but it doesn't propagate upward
# when using FetchContent. Need to reset it here so that macOS .app generation
# works properly.
set(rade_BINARY_DIR ${freedv_backend_BINARY_DIR}/rade_build)
