//=========================================================================
// Name:            DecodedSpeechStep.cpp
// Purpose:         Receives decoded speech audio for speech-to-text use.
//
// License:         BSD
//=========================================================================

#include "DecodedSpeechStep.h"

#include <algorithm>
#include <vector>

#include "../os/os_interface.h"

DecodedSpeechStep::DecodedSpeechStep(int inputSampleRate)
    : inputSampleRate_(inputSampleRate)
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

    constexpr int WORK_CHUNK_MS = 100;
    const int workChunkSamples =
        std::max(1, inputSampleRate_ * WORK_CHUNK_MS / 1000);

    std::vector<short> workBuffer(workChunkSamples);

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

            // Speech recognition will consume workBuffer here.
        }

        workerSem_.wait();
    }
}
