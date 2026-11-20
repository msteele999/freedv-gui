//=========================================================================
// Name:            IRigFrequencyController.h
// Purpose:         Interface for control of frequency/mode on radios.
//
// Authors:         Mooneer Salem
// License:
//
//  All rights reserved.
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
//=========================================================================

#ifndef I_RIG_FREQUENCY_CONTROLLER_H
#define I_RIG_FREQUENCY_CONTROLLER_H

#include <atomic>
#include <chrono>
#include <cstdint>

#include "IRigController.h"

class IRigFrequencyController : virtual public IRigController
{
public:
    enum Mode
    {
        UNKNOWN,
        
        USB,
        DIGU,
        LSB,
        DIGL,
        FM,
        DIGFM,
        AM,
    };
    
    virtual ~IRigFrequencyController() = default;
    
    // Event handlers that are called by implementers of this interface.
    EventHandler<IRigFrequencyController*, uint64_t, Mode> onFreqModeChange;
    
    virtual void setFrequency(uint64_t frequency, bool receiveOnly = false) = 0;
    // receiveOnly is used by the sideband override. It must never unkey a
    // transmitting rig; DIGL may fall back to LSB on an unsupported command.
    virtual void setMode(Mode mode, bool receiveOnly = false) = 0;
    virtual void requestCurrentFrequencyMode() = 0;

    // Also covers PTT controlled by a different controller (e.g. serial PTT
    // with OmniRig). Set before TX/Tune starts, clear on PTT-off acknowledgement.
    void setTransmitActive(bool active)
    {
        transmitActive_.store(active, std::memory_order_release);
        if (active) noteModeReadback_(UNKNOWN);
    }

    bool isTransmitActive() const
    {
        return transmitActive_.load(std::memory_order_acquire);
    }

    Mode getConfirmedMode() const
    {
        // A previously successful poll is not indefinite confirmation after
        // a cable is removed, a command fails, or polling stops during TX.
        if (isTransmitActive() || nowMs_() - modeReadAtMs_.load(std::memory_order_acquire) > 5000)
            return UNKNOWN;
        return confirmedMode_.load(std::memory_order_acquire);
    }

    // Whether a frequency reported by onFreqModeChange predates the one last
    // asked for with setFrequency(): it was read from the radio before that
    // change was made, and so says nothing about where the radio is now.
    // That's taken to be so until the radio reports the frequency asked for,
    // or for a few seconds at most (in case it can't be tuned exactly there,
    // or is tuned elsewhere meanwhile).
    bool isFrequencyChangePending(uint64_t reportedFrequency)
    {
        auto requested = requestedFrequency_.load(std::memory_order_acquire);
        if (requested == 0)
        {
            return false;
        }

        auto age = nowMs_() - requestedAtMs_.load(std::memory_order_acquire);
        if (reportedFrequency == requested || age > PENDING_FREQUENCY_TIMEOUT_MS)
        {
            requestedFrequency_.compare_exchange_strong(requested, 0, std::memory_order_acq_rel);
            return false;
        }
        return true;
    }

protected:
    IRigFrequencyController() = default;

    void noteModeReadback_(Mode mode)
    {
        modeReadAtMs_.store(nowMs_(), std::memory_order_release);
        confirmedMode_.store(mode, std::memory_order_release);
    }

    // For implementers' setFrequency().
    void noteRequestedFrequency_(uint64_t frequency)
    {
        requestedAtMs_.store(nowMs_(), std::memory_order_release);
        requestedFrequency_.store(frequency, std::memory_order_release);
    }

    // A mode command immediately following QSY must preserve that requested
    // dial, even if an asynchronous backend still reports its previous dial.
    uint64_t frequencyToPreserve_(uint64_t observed) const
    {
        const auto requested = requestedFrequency_.load(std::memory_order_acquire);
        return requested != 0 && nowMs_() - requestedAtMs_.load(std::memory_order_acquire) <= PENDING_FREQUENCY_TIMEOUT_MS
            ? requested : observed;
    }

private:
    static constexpr int64_t PENDING_FREQUENCY_TIMEOUT_MS = 3000;

    static int64_t nowMs_()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::atomic<uint64_t> requestedFrequency_{0};
    std::atomic<int64_t> requestedAtMs_{0};
    std::atomic<bool> transmitActive_{false};
    std::atomic<Mode> confirmedMode_{UNKNOWN};
    std::atomic<int64_t> modeReadAtMs_{0};
};

#endif // I_RIG_FREQUENCY_CONTROLLER_H
