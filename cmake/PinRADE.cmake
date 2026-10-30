# The backend stays at the validated RADEV1 revision. Freeze its previously
# floating rade_c dependency at the inspected dual-mode revision, without
# importing any of the RADEV2 backend's unrelated changes.
set(rade_revision "c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9")
set(build_rade_file "${BACKEND_SOURCE_DIR}/cmake/BuildRADE.cmake")
file(READ "${build_rade_file}" build_rade_contents)
if(build_rade_contents MATCHES "GIT_TAG main")
    string(REPLACE "GIT_TAG main" "GIT_TAG ${rade_revision}"
        build_rade_contents "${build_rade_contents}")
    file(WRITE "${build_rade_file}" "${build_rade_contents}")
elseif(NOT build_rade_contents MATCHES "GIT_TAG ${rade_revision}")
    message(FATAL_ERROR "Unexpected rade_c dependency declaration in the pinned FreeDV backend")
endif()
