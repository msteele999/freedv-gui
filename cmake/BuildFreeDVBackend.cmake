include(FetchContent)

FetchContent_Declare(
    freedv_backend
    GIT_REPOSITORY https://github.com/freedv/freedv-backend
    # Pin upstream v2.0-dev with the Reporter sideband API used by the GUI.
    # Keep a fixed commit for reproducible W2MWS builds.
    GIT_TAG f7a55fce6e4143f9bd11c63eaf361089c3a79cb2
)

FetchContent_MakeAvailable(freedv_backend)

# A populated source tree can be older than GIT_TAG when FetchContent is fully
# disconnected, or when FETCHCONTENT_SOURCE_DIR_FREEDV_BACKEND overrides it.
# Check the headers actually selected, without compiling or running anything.
function(freedv_require_backend_sideband_api)
    set(reporting_dir "${freedv_backend_SOURCE_DIR}/src/reporting")
    foreach(header IReporter.h FreeDVReporter.h)
        set(source "${reporting_dir}/${header}")
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "FreeDV backend reporting header not found: ${source}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
        file(READ "${source}" content)
        if(header STREQUAL "IReporter.h")
            set(required_api "enum class Sideband" "sidebandChange(")
        else()
            set(required_api "using SidebandChangeFn" "setOnSidebandChangeFn(" "setOnQSYRequestWithSidebandFn(")
        endif()
        foreach(api IN LISTS required_api)
            string(FIND "${content}" "${api}" position)
            if(position EQUAL -1)
                message(FATAL_ERROR
                    "The FreeDV backend at ${freedv_backend_SOURCE_DIR} lacks the required Reporter sideband API (${api}). "
                    "Reconfigure with -DFETCHCONTENT_FULLY_DISCONNECTED=OFF to populate the pinned backend, "
                    "or update FETCHCONTENT_SOURCE_DIR_FREEDV_BACKEND/the prepopulated sources to a compatible revision. "
                    "A stale dependency checkout cannot be used with this GUI.")
            endif()
        endforeach()
    endforeach()
endfunction()

freedv_require_backend_sideband_api()

# rade_BINARY_DIR is set in freedv_backend but it doesn't propagate upward
# when using FetchContent. Need to reset it here so that macOS .app generation
# works properly.
set(rade_BINARY_DIR ${freedv_backend_BINARY_DIR}/rade_build)
