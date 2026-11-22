# Keep the backend's rade_c dependency at the validated dual-mode revision,
# including when the backend is updated for stable Reporter fixes.
set(rade_revision "a225de306ed016b508111e8d51b32ae141c012ea")
set(build_rade_file "${BACKEND_SOURCE_DIR}/cmake/BuildRADE.cmake")
file(READ "${build_rade_file}" build_rade_contents)
if(build_rade_contents MATCHES "GIT_TAG main" OR
   build_rade_contents MATCHES "GIT_TAG c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9")
    string(REGEX REPLACE "GIT_TAG (main|c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9)"
        "GIT_TAG ${rade_revision}"
        build_rade_contents "${build_rade_contents}")
    file(WRITE "${build_rade_file}" "${build_rade_contents}")
elseif(NOT build_rade_contents MATCHES "GIT_TAG ${rade_revision}")
    message(FATAL_ERROR "Unexpected rade_c dependency declaration in the pinned FreeDV backend")
endif()
