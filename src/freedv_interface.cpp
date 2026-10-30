//==========================================================================
// Name:            freedv_interface.cpp
// Purpose:         Implements a wrapper around the Codec2 FreeDV interface.
// Created:         March 31, 2021
// Authors:         Mooneer Salem
// 
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================

#if defined(__clang__)
#if defined(__has_feature) && __has_feature(realtime_sanitizer)
#include <sanitizer/rtsan_interface.h>
#endif // defined(__has_feature) && __has_feature(realtime_sanitizer)
#endif // defined(__clang__)

#include <future>
#include <atomic>
#include <new>

#include "main.h"
#include "lpcnet.h"
#include "pipeline/RADEReceiveStep.h"
#include "pipeline/RADETransmitStep.h"
#include "pipeline/radev2/RADEV2ReceiveStep.h"
#include "pipeline/radev2/RADEV2TransmitStep.h"
#include "pipeline/AudioPipeline.h"
#include "pipeline/BandwidthExpandStep.h"
#include "pipeline/EitherOrStep.h"

#include "util/logging/ulog.h"

using namespace std::placeholders;

#include <wx/string.h>
extern wxString utFreeDVMode;

extern std::atomic<bool> g_bwExpandEnabled;

FreeDVInterface::FreeDVInterface() :
    textRxFunc_(nullptr),
    modemStatsList_(nullptr),
    rade_(nullptr),
    lpcnetEncState_(nullptr),
    radeTxStep_(nullptr),
    radeV2TxStep_(nullptr),
    activeMode_(RadeMode::RADEV1),
    sync_(0),
    radeSnr_(0),
    rxFreqOffset_(0.0f),
    radeTextPtr_(nullptr),
    radeV2TextPtr_(nullptr),
    reliableTextFifo_(RELIABLE_TEXT_FIFO_SIZE + 1)
{
    // empty
}

FreeDVInterface::~FreeDVInterface()
{
    if (isRunning()) stop();
}

void FreeDVInterface::OnRadeTextRx_(rade_text_t, const char* txt_ptr, int, void* state) 
{
    log_info("FreeDVInterface::OnRadeTextRx_: received %s", txt_ptr);
    
    FreeDVInterface* obj = (FreeDVInterface*)state;
    assert(obj != nullptr);
    
    char tmpBuf[RELIABLE_TEXT_FIFO_SIZE];
    memset(tmpBuf, 0, RELIABLE_TEXT_FIFO_SIZE);
    strncpy(tmpBuf, txt_ptr, RELIABLE_TEXT_FIFO_SIZE - 1);
    obj->reliableTextFifo_.write(tmpBuf, RELIABLE_TEXT_FIFO_SIZE);
}

void FreeDVInterface::resetReliableText()
{
    std::unique_lock<std::mutex> lock(reliableTextMutex_);
    receivedReliableText_ = "";
    reliableTextFifo_.reset();
}

const char* FreeDVInterface::getReliableText()
{    
    std::unique_lock<std::mutex> lock(reliableTextMutex_);
    
    if (reliableTextFifo_.numUsed() > 0)
    {
        char tmpBuf[RELIABLE_TEXT_FIFO_SIZE] = { 0 };
        if (reliableTextFifo_.read(tmpBuf, RELIABLE_TEXT_FIFO_SIZE) == 0)
        {
            receivedReliableText_ = tmpBuf;
        }
    }
    
    char* ret = new char[receivedReliableText_.size() + 1];
    assert(ret != nullptr);
    
    if (receivedReliableText_.size() > 0)
    {
        strncpy(ret, receivedReliableText_.c_str(), receivedReliableText_.size());
    }
    ret[receivedReliableText_.size()] = 0;
    return ret;
}

bool FreeDVInterface::start(int, bool usingReliableText, bool verbose, RadeMode mode)
{
    if (isRunning() || !RadeModes::CanStart(mode)) return false;

    sync_.store(0, std::memory_order_release);
    radeSnr_.store(0, std::memory_order_release);
    rxFreqOffset_.store(0.0f, std::memory_order_release);
    resetReliableText();

    try
    {
        modemStatsList_ = new MODEM_STATS[1];
        modem_stats_open(&modemStatsList_[0]);

        // The pinned rade_c contains both sets of built-in model weights. Its
        // per-instance flag initializes only the selected encoder/decoder/sync.
        char modelFile[1];
        modelFile[0] = 0;
        const int flags = RADE_USE_C_ENCODER | RADE_USE_C_DECODER |
            (mode == RadeMode::RADEV2 ? RADE_MODE_V2 : 0) | (verbose ? 0 : RADE_VERBOSE_0);
        rade_ = rade_open(modelFile, flags);
        if (rade_ == nullptr)
        {
            stop();
            return false;
        }
        if (mode == RadeMode::RADEV2 &&
            (rade_n_eoo_bits(rade_) != 0 || rade_n_features_in_out(rade_) <= 0 ||
             rade_n_tx_out(rade_) <= 0 || rade_nin_max(rade_) <= 0))
        {
            // Refuse an incompatible implementation; never fall back to V1.
            stop();
            return false;
        }

        if (usingReliableText)
        {
            log_info("creating RADE text object");
            if (mode == RadeMode::RADEV2)
            {
                radeV2TextPtr_ = rade_v2_text_create();
                if (radeV2TextPtr_ == nullptr)
                {
                    stop();
                    return false;
                }
                rade_v2_text_set_rx_callback(radeV2TextPtr_, &FreeDVInterface::OnRadeTextRx_, this);
                rade_v2_text_enable_stats_output(radeV2TextPtr_, utFreeDVMode != "");
            }
            else
            {
                radeTextPtr_ = rade_text_create();
                if (radeTextPtr_ == nullptr)
                {
                    stop();
                    return false;
                }
                rade_text_set_rx_callback(radeTextPtr_, &FreeDVInterface::OnRadeTextRx_, this);
                if (utFreeDVMode != "")
                {
                    rade_text_enable_stats_output(radeTextPtr_, true);
                }
            }
        }

        float zeros[320] = {0};
        float in_features[5*NB_TOTAL_FEATURES] = {0};
        fargan_init(&fargan_);
        fargan_cont(&fargan_, zeros, in_features);

        lpcnetEncState_ = lpcnet_encoder_create();
        if (lpcnetEncState_ == nullptr)
        {
            stop();
            return false;
        }
    }
    catch (const std::bad_alloc&)
    {
        stop();
        return false;
    }

    // Publish only after initialization succeeds. The persisted selection is
    // never used as the active protocol identity by status or Reporter.
    activeMode_.store(mode, std::memory_order_release);
    return true;
}

void FreeDVInterface::stop()
{
    if (modemStatsList_ != nullptr)
    {
        modem_stats_close(&modemStatsList_[0]);
        delete[] modemStatsList_;
    }
    modemStatsList_ = nullptr;
    
    if (radeTextPtr_ != nullptr)
    {
        rade_text_destroy(radeTextPtr_);
        radeTextPtr_ = nullptr;
    }
    
    if (radeV2TextPtr_ != nullptr)
    {
        rade_v2_text_destroy(radeV2TextPtr_);
        radeV2TextPtr_ = nullptr;
    }
    if (rade_ != nullptr)
    {
        rade_close(rade_);
    }
    rade_ = nullptr;

    if (lpcnetEncState_ != nullptr)
    {
        lpcnet_encoder_destroy(lpcnetEncState_);
    }
    lpcnetEncState_ = nullptr;
    radeTxStep_ = nullptr;
    radeV2TxStep_ = nullptr;
}

int FreeDVInterface::getTotalBits()
{
    // Special case for RADE.
    return 1;
}

int FreeDVInterface::getTotalBitErrors()
{
    // Special case for RADE.
    return 0;
}

float FreeDVInterface::getVariance() const
{
    // Special case for RADE.
    return 0.0;
}

const char* FreeDVInterface::getCurrentModeStr() const
{
    return RadeModes::ProtocolName(activeMode_.load(std::memory_order_acquire));
}

const char* FreeDVInterface::getCurrentTxModeStr() const
{
    return RadeModes::ProtocolName(activeMode_.load(std::memory_order_acquire));
}

int FreeDVInterface::getSync() const
{
    return sync_.load(std::memory_order_acquire);
}

int FreeDVInterface::getTxModemSampleRate() const FREEDV_NONBLOCKING
{
    return RADE_MODEM_SAMPLE_RATE;
}

int FreeDVInterface::getTxSpeechSampleRate() const
{
    return RADE_SPEECH_SAMPLE_RATE;
}

int FreeDVInterface::getTxNumSpeechSamples() const
{
    return rade_n_features_in_out(rade_) * LPCNET_FRAME_SIZE / NB_TOTAL_FEATURES;
}

int FreeDVInterface::getTxNNomModemSamples() const FREEDV_NONBLOCKING
{
    // Verified that rade_api.c from librade has no unbounded operations
    // as of 2025-10-03.
    FREEDV_BEGIN_VERIFIED_SAFE
    if (radeV2TxStep_ != nullptr)
        return std::max(rade_n_tx_out(rade_), radeV2TxStep_->eooLengthInSamples());
    return std::max(rade_n_tx_out(rade_), radeTxStep_ != nullptr ? radeTxStep_->eooLengthInSamples() : rade_n_tx_eoo_out(rade_));
    FREEDV_END_VERIFIED_SAFE
}

int FreeDVInterface::getRxModemSampleRate() const
{
    return RADE_MODEM_SAMPLE_RATE;
}

int FreeDVInterface::getRxNumModemSamples() const
{
    return rade_nin_max(rade_);
}

int FreeDVInterface::getRxNumSpeechSamples() const FREEDV_NONBLOCKING
{
    return rade_n_features_in_out(rade_) * LPCNET_FRAME_SIZE / NB_TOTAL_FEATURES;
}

int FreeDVInterface::getRxSpeechSampleRate() const FREEDV_NONBLOCKING
{
    return RADE_SPEECH_SAMPLE_RATE;
}

void FreeDVInterface::setReliableText(const char* callsign)
{
    if (rade_ != nullptr && radeV2TextPtr_ != nullptr)
    {
        rade_v2_text_generate_tx_string(radeV2TextPtr_, callsign, strlen(callsign));
        return;
    }
    if (rade_ != nullptr && radeTextPtr_ != nullptr)
    {
        log_info("generating RADE text string");
        int nsyms = rade_n_eoo_bits(rade_);
        float* eooSyms = new float[nsyms];
        assert(eooSyms);

        rade_text_generate_tx_string(radeTextPtr_, callsign, strlen(callsign), eooSyms, nsyms);
        rade_tx_set_eoo_bits(rade_, eooSyms);

        delete[] eooSyms;
    }
}

float FreeDVInterface::getSNREstimate()
{
    return (getSync() ? radeSnr_.load(std::memory_order_acquire) : 0);
}

float FreeDVInterface::getCurrentRxModemOffset()
{
    // Read from the GUI thread, so use the copy the RX thread publishes
    // rather than reading the modem state it's updating.
    return rxFreqOffset_.load(std::memory_order_acquire);
}

IPipelineStep* FreeDVInterface::createTransmitPipeline(
    int inputSampleRate, 
    int outputSampleRate) 
{
    // Special handling for RADE. Note that ParallelStep is not being used
    // as it has known issues with Bluetooth and macOS (but only with RADE;
    // analog and legacy modes appear to function properly).
    auto pipeline = new AudioPipeline(inputSampleRate, outputSampleRate);
    if (activeMode_.load(std::memory_order_acquire) == RadeMode::RADEV2)
    {
        radeV2TxStep_ = new RADEV2TransmitStep(rade_, lpcnetEncState_, radeV2TextPtr_);
        pipeline->appendPipelineStep(radeV2TxStep_);
    }
    else
    {
        radeTxStep_ = new RADETransmitStep(rade_, lpcnetEncState_);
        pipeline->appendPipelineStep(radeTxStep_);
    }
    return pipeline;
}

IPipelineStep* FreeDVInterface::createReceivePipeline(
    int inputSampleRate, int outputSampleRate,
    realtime_fp<std::atomic<int>*()> const& getRxStateFn,
    realtime_fp<float()> const& getFreqOffsetFn,
    realtime_fp<float*()> const& getSigPwrAvgFn)
{
    std::shared_ptr<ReceivePipelineState> state = std::make_shared<ReceivePipelineState>();
    assert(state != nullptr);

    state->getRxStateFn = getRxStateFn;
    state->getFreqOffsetFn = getFreqOffsetFn;
    state->getSigPwrAvgFn = getSigPwrAvgFn;
   
    IPipelineStep* rxStep = nullptr;
    if (activeMode_.load(std::memory_order_acquire) == RadeMode::RADEV2)
    {
        auto v2Step = new RADEV2ReceiveStep(rade_, &fargan_, radeV2TextPtr_, +[](RADEV2ReceiveStep* step) FREEDV_NONBLOCKING {
            FreeDVInterface* state = (FreeDVInterface*)step->getStateObj();
            (*step->getRxStateFn()()).store(step->getSync(), std::memory_order_release);
            state->publishRxState_(step->getSync(), step->getSnr());
        }, getFreqOffsetFn);
        v2Step->setStateObj(this);
        v2Step->setRxStateFn(getRxStateFn);
        rxStep = v2Step;
    }
    else
    {
        auto v1Step = new RADEReceiveStep(rade_, &fargan_, radeTextPtr_, +[](RADEReceiveStep* step) FREEDV_NONBLOCKING {
            FreeDVInterface* state = (FreeDVInterface*)step->getStateObj();
            auto finalSync = step->getSync();
            (*step->getRxStateFn()()).store(finalSync, std::memory_order_release);
            state->publishRxState_(finalSync, step->getSnr());
        }, getFreqOffsetFn);
        v1Step->setStateObj(this);
        v1Step->setRxStateFn(getRxStateFn);
        rxStep = v1Step;
    }

    auto pipeline = new AudioPipeline(inputSampleRate, outputSampleRate);
    pipeline->appendPipelineStep(rxStep);
     
    auto bwExpandStep = new BandwidthExpandStep();
    auto bwExpandBypass = new AudioPipeline(bwExpandStep->getInputSampleRate(), bwExpandStep->getOutputSampleRate());
        
    auto eitherOrBwExpandStep = new EitherOrStep(
        +[]() FREEDV_NONBLOCKING { return g_bwExpandEnabled.load(std::memory_order_acquire); },
        bwExpandStep,
        bwExpandBypass
    );
    pipeline->appendPipelineStep(eitherOrBwExpandStep);

    return pipeline;
}

void FreeDVInterface::restartTxVocoder() FREEDV_NONBLOCKING
{ 
    if (radeV2TxStep_ != nullptr) radeV2TxStep_->restartVocoder();
    else if (radeTxStep_ != nullptr) radeTxStep_->restartVocoder();
}

void FreeDVInterface::publishRxState_(int sync, int snr) FREEDV_NONBLOCKING
{
    sync_.store(sync, std::memory_order_release);
    radeSnr_.store(snr, std::memory_order_release);
    // rade_freq_offset() dispatches to a simple getter for either mode.
    FREEDV_BEGIN_VERIFIED_SAFE
    rxFreqOffset_.store(rade_freq_offset(rade_), std::memory_order_release);
    FREEDV_END_VERIFIED_SAFE
}
