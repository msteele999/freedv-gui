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

DecodedSpeechStep::DecodedSpeechStep(int inputSampleRate, std::string modelPath, std::string vadModelPath,
                                     TranscriptCallback onTranscript)
    : inputSampleRate_(inputSampleRate)
    , modelPath_(std::move(modelPath))
    , vadModelPath_(std::move(vadModelPath))
    , onTranscript_(std::move(onTranscript))
    , inputFifo_(inputSampleRate * FIFO_SECONDS + 1)
    , workerEnding_(false)
    , samplesReceived_(0)
    , samplesProcessed_(0)
    , samplesDropped_(0)
{
    transcriptionThread_ =
        std::thread(&DecodedSpeechStep::transcriptionThreadEntry_, this);
    workerThread_ = std::thread(&DecodedSpeechStep::workerThreadEntry_, this);
}

DecodedSpeechStep::~DecodedSpeechStep()
{
    workerEnding_.store(true, std::memory_order_release);
    workerSem_.signal();
    transcriptionSem_.signal();

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    if (transcriptionThread_.joinable())
    {
        transcriptionThread_.join();
    }

    const auto dropped = samplesDropped_.load(std::memory_order_relaxed);
    if (dropped != 0)
    {
        log_warn("Decoded speech: audio FIFO dropped %llu samples",
                 static_cast<unsigned long long>(dropped));
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

    whisper_vad_context_params vadContextParams =
        whisper_vad_default_context_params();

    whisper_vad_context* vadContext =
        whisper_vad_init_from_file_with_params(
            vadModelPath_.c_str(),
            vadContextParams);

    if (vadContext == nullptr)
    {
        log_warn(
            "Decoded speech: unable to load VAD model from %s",
            vadModelPath_.c_str());
    }
    else
    {
        log_info(
            "Decoded speech: VAD model loaded from %s",
            vadModelPath_.c_str());
    }

    constexpr int WORK_CHUNK_MS = 100;
    const int workChunkSamples =
        std::max(1, inputSampleRate_ * WORK_CHUNK_MS / 1000);

    std::vector<short> workBuffer(workChunkSamples);

    constexpr float VAD_START_THRESHOLD = 0.50f;
    constexpr float VAD_END_THRESHOLD = 0.35f;
    constexpr int TRAILING_SILENCE_MS = 500;
    constexpr int MIN_SPEECH_MS = 250;

    const int trailingSilenceChunks =
        std::max(1, TRAILING_SILENCE_MS / WORK_CHUNK_MS);
    // Round up: two 100 ms chunks do not meet the 250 ms minimum.
    const int minimumSpeechChunks =
        std::max(1, (MIN_SPEECH_MS + WORK_CHUNK_MS - 1) / WORK_CHUNK_MS);

    std::vector<short> transcriptionBuffer;
    std::vector<float> vadPcm(workChunkSamples);

    bool speechActive = false;
    int speechChunks = 0;
    int silenceChunks = 0;

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

            if (vadContext == nullptr)
            {
                continue;
            }

            for (size_t i = 0; i < workBuffer.size(); ++i)
            {
                vadPcm[i] =
                    static_cast<float>(workBuffer[i]) / 32768.0f;
            }

            if (!whisper_vad_detect_speech_no_reset(
                    vadContext,
                    vadPcm.data(),
                    static_cast<int>(vadPcm.size())))
            {
                log_warn("Decoded speech: VAD processing failed");
                continue;
            }

            const int probabilityCount =
                whisper_vad_n_probs(vadContext);
            float* probabilities =
                whisper_vad_probs(vadContext);

            float maxProbability = 0.0f;
            for (int i = 0; i < probabilityCount; ++i)
            {
                maxProbability =
                    std::max(maxProbability, probabilities[i]);
            }

            if (!speechActive)
            {
                if (maxProbability >= VAD_START_THRESHOLD)
                {
                    speechActive = true;
                    speechChunks = 1;
                    silenceChunks = 0;

                    transcriptionBuffer.insert(
                        transcriptionBuffer.end(),
                        workBuffer.begin(),
                        workBuffer.end());
                }

                continue;
            }

            transcriptionBuffer.insert(
                transcriptionBuffer.end(),
                workBuffer.begin(),
                workBuffer.end());

            if (maxProbability >= VAD_END_THRESHOLD)
            {
                ++speechChunks;
                silenceChunks = 0;
            }
            else
            {
                ++silenceChunks;
            }

            if (silenceChunks >= trailingSilenceChunks)
            {
                if (speechChunks >= minimumSpeechChunks)
                {
                    bool queued = false;

                    {
                        std::lock_guard<std::mutex> lock(transcriptionMutex_);

                        if (transcriptionQueue_.size() < MAX_TRANSCRIPTION_QUEUE)
                        {
                            transcriptionQueue_.push_back(
                                std::move(transcriptionBuffer));
                            queued = true;
                        }
                    }

                    if (queued)
                    {
                        transcriptionSem_.signal();
                    }
                    else
                    {
                        log_warn("Decoded speech: transcription queue full, dropping utterance");
                    }
                }

                transcriptionBuffer.clear();
                speechActive = false;
                speechChunks = 0;
                silenceChunks = 0;
                whisper_vad_reset_state(vadContext);
            }
        }

        workerSem_.wait();
    }

    if (vadContext != nullptr)
    {
        whisper_vad_free(vadContext);
    }

}

void DecodedSpeechStep::transcriptionThreadEntry_()
{
    SetThreadName("DecodedSpeechSTT");

    whisper_context_params contextParams = whisper_context_default_params();
    contextParams.use_gpu = false;

    whisper_context* whisperContext =
        whisper_init_from_file_with_params(modelPath_.c_str(), contextParams);

    if (whisperContext == nullptr)
    {
        log_warn(
            "Decoded speech: unable to load Whisper model from %s",
            modelPath_.c_str());
    }
    else
    {
        log_info(
            "Decoded speech: Whisper model loaded from %s",
            modelPath_.c_str());
    }

    while (!workerEnding_.load(std::memory_order_acquire))
    {
        std::vector<short> audio;

        {
            std::lock_guard<std::mutex> lock(transcriptionMutex_);

            if (!transcriptionQueue_.empty())
            {
                audio = std::move(transcriptionQueue_.front());
                transcriptionQueue_.pop_front();
            }
        }

        if (audio.empty())
        {
            transcriptionSem_.wait();
            continue;
        }

        if (whisperContext == nullptr)
        {
            continue;
        }

        std::vector<float> pcmf32(audio.size());

        for (size_t i = 0; i < audio.size(); ++i)
        {
            pcmf32[i] =
                static_cast<float>(audio[i]) / 32768.0f;
        }

        whisper_full_params params =
            whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

        params.language = "en";
        params.translate = false;
        params.no_context = true;
        params.initial_prompt =
            "Amateur radio digital voice: CQ, QSO, QTH, QRM, QRN, QSB, QRP, QRO, "
            "SNR, FreeDV, RADE, USB, LSB, signal report, five by nine, 73, Roger, copy, over. "
            "Alpha, Bravo, Charlie, Delta, Echo, Foxtrot, Golf, Hotel, India, Juliett, "
            "Kilo, Lima, Mike, November, Oscar, Papa, Quebec, Romeo, Sierra, Tango, "
            "Uniform, Victor, Whiskey, X-ray, Yankee, Zulu.";
        params.no_timestamps = true;
        params.print_progress = false;
        params.print_realtime = false;
        params.print_timestamps = false;

        // 16 kHz PCM, 320 samples per encoder context position.
        // Keep two seconds of capacity beyond the entire VAD utterance,
        // including retained silence. Subtraction avoids sample-count overflow.
        constexpr size_t CONTEXT_MARGIN_SAMPLES = 32000;
        constexpr size_t SAMPLES_PER_AUDIO_CONTEXT = 320;
        params.audio_ctx = 0; // Select independently for every utterance.
        if (pcmf32.size() <= 500 * SAMPLES_PER_AUDIO_CONTEXT - CONTEXT_MARGIN_SAMPLES)
        {
            params.audio_ctx = 500;
        }

        const int whisperResult = whisper_full(
            whisperContext,
            params,
            pcmf32.data(),
            static_cast<int>(pcmf32.size()));

        if (whisperResult == 0)
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
                if (onTranscript_)
                    onTranscript_(text);
            }
        }
        else
        {
            log_warn("Decoded speech: Whisper transcription failed (%d)", whisperResult);
        }
    }

    if (whisperContext != nullptr)
    {
        whisper_free(whisperContext);
    }
}
