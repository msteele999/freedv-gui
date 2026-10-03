# Architecture-independent model weights, pinned to the existing development models.
set(FREEDV_SPEECH_MODEL_DIR "${CMAKE_BINARY_DIR}/models")
file(MAKE_DIRECTORY "${FREEDV_SPEECH_MODEL_DIR}")

function(freedv_acquire_speech_model name url sha256)
    set(destination "${FREEDV_SPEECH_MODEL_DIR}/${name}")
    # Reuse verified downloads, including the existing development model directory.
    foreach(candidate IN ITEMS "${destination}" "${CMAKE_CURRENT_BINARY_DIR}/models/${name}"
            "${whisper_SOURCE_DIR}/models/${name}")
        if(EXISTS "${candidate}")
            file(SHA256 "${candidate}" actual_sha256)
            if(actual_sha256 STREQUAL sha256)
                if(NOT candidate STREQUAL destination)
                    configure_file("${candidate}" "${destination}" COPYONLY)
                endif()
                return()
            endif()
        endif()
    endforeach()

    file(DOWNLOAD "${url}" "${destination}.download"
        EXPECTED_HASH "SHA256=${sha256}" TLS_VERIFY ON SHOW_PROGRESS
        STATUS download_status)
    list(GET download_status 0 download_result)
    if(NOT download_result EQUAL 0)
        file(REMOVE "${destination}.download")
        message(FATAL_ERROR "Unable to acquire speech model ${name}: ${download_status}")
    endif()
    file(RENAME "${destination}.download" "${destination}")
endfunction()

freedv_acquire_speech_model(ggml-base.en.bin
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-base.en.bin"
    a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002)
freedv_acquire_speech_model(ggml-silero-v6.2.0.bin
    "https://huggingface.co/ggml-org/whisper-vad/resolve/9ffd54a1e1ee413ddf265af9913beaf518d1639b/ggml-silero-v6.2.0.bin"
    2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987)

set(FREEDV_SPEECH_MODELS
    "${FREEDV_SPEECH_MODEL_DIR}/ggml-base.en.bin"
    "${FREEDV_SPEECH_MODEL_DIR}/ggml-silero-v6.2.0.bin")
