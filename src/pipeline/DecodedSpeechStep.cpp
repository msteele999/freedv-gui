//=========================================================================
// Name:            DecodedSpeechStep.cpp
// Purpose:         Receives decoded speech audio for speech-to-text use.
//
// License:         BSD
//=========================================================================

#include "DecodedSpeechStep.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "../os/os_interface.h"
#include "../util/logging/ulog.h"

#include "whisper.h"

DecodedSpeechStep::DecodedSpeechStep(int inputSampleRate, std::string modelPath)
    : inputSampleRate_(inputSampleRate)
    , modelPath_(std::move(modelPath))
    , inputFifo_(inputSampleRate * FIFO_SECONDS + 1)
    , workerEnding_(false)
    , samplesReceived_(0)
    , samplesProcessed_(0)
    , samplesDropped_(0)
{
    workerThread_ = std::thread(&DecodedSpeechStep::workerThreadEntry_, this);
}

DecodedSpeechStep::~DecodedSpeechStep()
{
    workerEnding_.store(true, std::memory_order_release);
    workerSem_.signal();

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }
}

int DecodedSpeechStep::getInputSampleRate() const FREEDV_NONBLOCKING
{
    return inputSampleRate_;
}

int DecodedSpeechStep::getOutputSampleRate() const FREEDV_NONBLOCKING
{
    return inputSampleRate_;
}

short* DecodedSpeechStep::execute(
    short* inputSamples,
    int numInputSamples,
    int* numOutputSamples) FREEDV_NONBLOCKING
{
    samplesReceived_.fetch_add(
        static_cast<uint64_t>(numInputSamples),
        std::memory_order_relaxed);

    if (inputFifo_.write(inputSamples, numInputSamples) != 0)
    {
        samplesDropped_.fetch_add(
            static_cast<uint64_t>(numInputSamples),
            std::memory_order_relaxed);
    }
    else
    {
        workerSem_.signal();
    }

    *numOutputSamples = 0;
    return nullptr;
}

void DecodedSpeechStep::reset() FREEDV_NONBLOCKING
{
    inputFifo_.reset();

    samplesReceived_.store(0, std::memory_order_relaxed);
    samplesProcessed_.store(0, std::memory_order_relaxed);
    samplesDropped_.store(0, std::memory_order_relaxed);
}

uint64_t DecodedSpeechStep::getSamplesReceived() const FREEDV_NONBLOCKING
{
    return samplesReceived_.load(std::memory_order_relaxed);
}

uint64_t DecodedSpeechStep::getSamplesProcessed() const FREEDV_NONBLOCKING
{
    return samplesProcessed_.load(std::memory_order_relaxed);
}

uint64_t DecodedSpeechStep::getSamplesDropped() const FREEDV_NONBLOCKING
{
    return samplesDropped_.load(std::memory_order_relaxed);
}

void DecodedSpeechStep::workerThreadEntry_()
{
    SetThreadName("DecodedSpeech");

    whisper_context_params contextParams = whisper_context_default_params();
    contextParams.use_gpu = false;

    whisper_context* whisperContext =
        whisper_init_from_file_with_params(modelPath_.c_str(), contextParams);

    if (whisperContext == nullptr)
    {
        log_info(
            "Decoded speech: unable to load Whisper model from %s",
            modelPath_.c_str());
    }
    else
    {
        log_info(
            "Decoded speech: Whisper model loaded from %s",
            modelPath_.c_str());
    }

    constexpr int WORK_CHUNK_MS = 100;
    const int workChunkSamples =
        std::max(1, inputSampleRate_ * WORK_CHUNK_MS / 1000);

    std::vector<short> workBuffer(workChunkSamples);

    constexpr int TRANSCRIPTION_SECONDS = 5;
    const int transcriptionSamples = inputSampleRate_ * TRANSCRIPTION_SECONDS;
    std::vector<short> transcriptionBuffer;
    transcriptionBuffer.reserve(transcriptionSamples);

    while (!workerEnding_.load(std::memory_order_acquire))
    {
        while (inputFifo_.numUsed() >= workChunkSamples)
        {
            if (inputFifo_.read(workBuffer.data(), workChunkSamples) != 0)
            {
                break;
            }

            samplesProcessed_.fetch_add(
                static_cast<uint64_t>(workChunkSamples),
                std::memory_order_relaxed);

            transcriptionBuffer.insert(
                transcriptionBuffer.end(),
                workBuffer.begin(),
                workBuffer.end());

            if (whisperContext != nullptr &&
                static_cast<int>(transcriptionBuffer.size()) >= transcriptionSamples)
            {
                std::vector<float> pcmf32(transcriptionBuffer.size());

                for (size_t i = 0; i < transcriptionBuffer.size(); ++i)
                {
                    pcmf32[i] =
                        static_cast<float>(transcriptionBuffer[i]) / 32768.0f;
                }

                whisper_full_params params =
                    whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

                params.language = "en";
                params.translate = false;
                params.no_context = true;
                params.no_timestamps = true;
                params.print_progress = false;
                params.print_realtime = false;
                params.print_timestamps = false;

                if (whisper_full(
                        whisperContext,
                        params,
                        pcmf32.data(),
                        static_cast<int>(pcmf32.size())) == 0)
                {
                    std::string text;
                    const int segmentCount =
                        whisper_full_n_segments(whisperContext);

                    for (int i = 0; i < segmentCount; ++i)
                    {
                        text += whisper_full_get_segment_text(
                            whisperContext, i);
                    }

                    if (!text.empty())
                    {
                        log_info("Decoded speech:%s", text.c_str());
                    }
                }

                transcriptionBuffer.clear();
            }
        }

        workerSem_.wait();
    }

    if (whisperContext != nullptr)
    {
        whisper_free(whisperContext);
    }
}
