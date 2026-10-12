# Backport upstream GGML UBSAN fix a2b9a22e.
if(NOT EXISTS "${GGML_SOURCE}")
    message(FATAL_ERROR "GGML source not found: ${GGML_SOURCE}")
endif()

file(READ "${GGML_SOURCE}" content)

set(function_start "static size_t ggml_graph_nbytes(")
string(FIND "${content}" "${function_start}" start)

if(start EQUAL -1)
    message(FATAL_ERROR "ggml_graph_nbytes function not found")
endif()

string(SUBSTRING "${content}" ${start} -1 remainder)
string(FIND "${remainder}" "\n}" end)

if(end EQUAL -1)
    message(FATAL_ERROR "ggml_graph_nbytes function end not found")
endif()

math(EXPR length "${end} + 2")
string(SUBSTRING "${remainder}" 0 ${length} original)

set(patched "${original}")

set(old_start "void * p = 0;")
set(new_start "void * p = (char *) 1024; // workaround for ubsan error \"applying non-zero offset X to null pointer\"")
set(old_end "return nbytes;")
set(new_end "return nbytes - 1024;")

string(FIND "${original}" "${old_start}" old_start_pos)
string(FIND "${original}" "${new_start}" new_start_pos)
string(FIND "${original}" "${old_end}" old_end_pos)
string(FIND "${original}" "${new_end}" new_end_pos)

if(NOT old_start_pos EQUAL -1 AND NOT old_end_pos EQUAL -1
   AND new_start_pos EQUAL -1 AND new_end_pos EQUAL -1)
    string(REPLACE "${old_start}" "${new_start}" patched "${patched}")
    string(REPLACE "${old_end}" "${new_end}" patched "${patched}")
    string(REPLACE "${original}" "${patched}" content "${content}")
    file(WRITE "${GGML_SOURCE}" "${content}")
    message(STATUS "Applied GGML UBSAN patch")
elseif(NOT new_start_pos EQUAL -1 AND NOT new_end_pos EQUAL -1
       AND old_start_pos EQUAL -1 AND old_end_pos EQUAL -1)
    message(STATUS "GGML UBSAN patch already applied")
else()
    message(FATAL_ERROR "Unexpected ggml_graph_nbytes implementation")
endif()
