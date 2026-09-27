//=========================================================================
// Name:            DecodedSpeechStep.h
// Purpose:         Receives decoded speech audio for speech-to-text use.
//
// License:         BSD
//=========================================================================

#ifndef AUDIO_PIPELINE__DECODED_SPEECH_STEP_H
#define AUDIO_PIPELINE__DECODED_SPEECH_STEP_H

#include "IPipelineStep.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "../util/GenericFIFO.h"
#include "../util/Semaphore.h"

class DecodedSpeechStep : public IPipelineStep
{
public:
    DecodedSpeechStep(int inputSampleRate, std::string modelPath, std::string vadModelPath);
    virtual ~DecodedSpeechStep();

    virtual int getInputSampleRate() const FREEDV_NONBLOCKING override;
    virtual int getOutputSampleRate() const FREEDV_NONBLOCKING override;
    virtual short* execute(
        short* inputSamples,
        int numInputSamples,
        int* numOutputSamples) FREEDV_NONBLOCKING override;
    virtual void reset() FREEDV_NONBLOCKING override;

    uint64_t getSamplesReceived() const FREEDV_NONBLOCKING;
    uint64_t getSamplesProcessed() const FREEDV_NONBLOCKING;
    uint64_t getSamplesDropped() const FREEDV_NONBLOCKING;

private:
    static constexpr int FIFO_SECONDS = 2;

    int inputSampleRate_;
    std::string modelPath_;
    std::string vadModelPath_;
    GenericFIFO<short> inputFifo_;
    Semaphore workerSem_;
    std::thread workerThread_;
    std::atomic<bool> workerEnding_;

    std::atomic<uint64_t> samplesReceived_;
    std::atomic<uint64_t> samplesProcessed_;
    std::atomic<uint64_t> samplesDropped_;

    void workerThreadEntry_();
};

#endif // AUDIO_PIPELINE__DECODED_SPEECH_STEP_H
