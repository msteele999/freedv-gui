include(FetchContent)

FetchContent_Declare(
    freedv_backend
    GIT_REPOSITORY https://github.com/freedv/freedv-backend
    # Pin the upstream v2.0-dev backend for reproducible builds.
    GIT_TAG 9922bba3aed07c4b3d13ef7a4b003f917f253ded
)

FetchContent_MakeAvailable(freedv_backend)

# rade_BINARY_DIR is set in freedv_backend but it doesn't propagate upward
# when using FetchContent. Need to reset it here so that macOS .app generation
# works properly.
set(rade_BINARY_DIR ${freedv_backend_BINARY_DIR}/rade_build)
