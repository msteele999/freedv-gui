# The pinned GGML casts typed kernels to callbacks with void-pointer arguments.
# Calling through those incompatible function types is undefined behavior, even
# when their platform ABIs agree. Adapters keep the kernels and sanitizers intact.
function(freedv_patch_whisper_cpu source)
    file(READ "${source}" original)
    set(content "${original}")
    set(adapters "// FreeDV: type-safe adapters for GGML CPU trait callbacks.\n")

    foreach(spec "f32,float,fp32" "f16,ggml_fp16_t,fp16" "bf16,ggml_bf16_t,bf16")
        string(REPLACE "," ";" fields "${spec}")
        list(GET fields 0 suffix)
        list(GET fields 1 scalar)
        list(GET fields 2 conversion)

        # The pinned f16/bf16 dot kernels take non-const pointers but only read
        # the input buffers; retain their signatures for all direct callers.
        set(input_type "${scalar}")
        if(suffix STREQUAL "f32")
            set(input_type "const ${scalar}")
        endif()
        string(CONFIGURE [=[
static void freedv_from_float_@suffix@(const float * x, void * y, int64_t n) {
    ggml_cpu_fp32_to_@conversion@(x, (@scalar@ *) y, n);
}

static void freedv_vec_dot_@suffix@(int n, float * s, size_t bs, const void * x, size_t bx,
                                  const void * y, size_t by, int nrc) {
    ggml_vec_dot_@suffix@(n, s, bs, (@input_type@ *) x, bx, (@input_type@ *) y, by, nrc);
}

]=] adapter @ONLY)
        string(APPEND adapters "${adapter}")

        foreach(kind from_float vec_dot)
            if(kind STREQUAL "from_float")
                set(old "(ggml_from_float_t) ggml_cpu_fp32_to_${conversion}")
            else()
                set(old "(ggml_vec_dot_t) ggml_vec_dot_${suffix}")
            endif()
            set(new "freedv_${kind}_${suffix}")
            string(FIND "${content}" "${old}" old_pos)
            string(FIND "${content}" "= ${new}," new_pos)
            if(NOT old_pos EQUAL -1 AND new_pos EQUAL -1)
                string(REPLACE "${old}" "${new}" content "${content}")
            elseif(old_pos EQUAL -1 AND NOT new_pos EQUAL -1)
                # Already patched.
            else()
                message(FATAL_ERROR "Unexpected GGML CPU ${kind}/${suffix} callback in ${source}")
            endif()
        endforeach()
    endforeach()

    set(anchor "static const struct ggml_type_traits_cpu type_traits_cpu[GGML_TYPE_COUNT] = {")
    string(FIND "${content}" "${anchor}" anchor_pos)
    if(anchor_pos EQUAL -1)
        message(FATAL_ERROR "GGML CPU type traits not found: ${source}")
    endif()
    string(FIND "${content}" "${adapters}" adapters_pos)
    if(adapters_pos EQUAL -1)
        string(FIND "${content}" "static void freedv_" partial_pos)
        if(NOT partial_pos EQUAL -1)
            message(FATAL_ERROR "Unexpected GGML CPU adapters in ${source}")
        endif()
        string(REPLACE "${anchor}" "${adapters}${anchor}" content "${content}")
    endif()

    if(NOT content STREQUAL original)
        file(WRITE "${source}" "${content}")
        message(STATUS "Applied GGML CPU callback type fix")
    endif()
endfunction()
