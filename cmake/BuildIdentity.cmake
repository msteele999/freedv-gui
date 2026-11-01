# Identity for the experimental/radev2 source tree only.
# Preserve an explicit CMake command-line version override.
if(NOT FREEDV_BUILD_VERSION_OVERRIDE)
    set(FREEDV_BUILD_VERSION_OVERRIDE "${PROJECT_VERSION}-dev-W2MWS-radev2.3")
endif()
