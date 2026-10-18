# Suppress the OpenSSL deprecation warning for hostname verification.
# Preserve SSL_set1_host behavior on all supported platforms.

if(NOT EXISTS "${BACKEND_SOURCE}")
    message(FATAL_ERROR "Backend source not found: ${BACKEND_SOURCE}")
endif()

file(READ "${BACKEND_SOURCE}" content)

set(original
    "                    if (!SSL_set1_host(ssl_.load(std::memory_order_relaxed), host_.c_str()))")

set(patched [=[
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
                    if (!SSL_set1_host(ssl_.load(std::memory_order_relaxed), host_.c_str()))
#if defined(__clang__)
#pragma clang diagnostic pop
#endif]=])

string(FIND "${content}" "${original}" old_pos)
string(FIND "${content}" "${patched}" new_pos)

if(NOT new_pos EQUAL -1)
    message(STATUS "Backend OpenSSL workaround already applied")
elseif(NOT old_pos EQUAL -1)
    string(REPLACE "${original}" "${patched}" content "${content}")
    file(WRITE "${BACKEND_SOURCE}" "${content}")
    message(STATUS "Applied backend OpenSSL deprecation workaround")
else()
    message(FATAL_ERROR "Unexpected backend OpenSSL hostname verification implementation")
endif()
