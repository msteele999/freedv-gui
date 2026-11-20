#include "../omnirig/OmniRigController.h"
#include "../../test/UnitTestCommon.h"
#include <functional>
#include <future>

// A COM stand-in; no installed OmniRig server, serial port, or radio required.
struct FakeOmniRig : IRigX
{
    long frequency = 7177000;
    RigParamX mode = PM_SSB_U;
    long writable = PM_DIG_L | PM_SSB_L | PM_SSB_U;
    long readable = PM_DIG_L | PM_SSB_L | PM_SSB_U;
    RigStatusX status = ST_ONLINE;
    HRESULT diglResult = S_OK;
    HRESULT readResult = S_OK;
    int diglRequests = 0, modeRequests = 0, pttRequests = 0;
    bool ignoreWrites = false;
    bool staleFrequencyReadback = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_RigType(BSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_ReadableParams(long* value) override { *value = readable; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_WriteableParams(long* value) override { *value = writable; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsParamReadable(RigParamX p, VARIANT_BOOL* value) override
    { *value = (readable & p) ? VARIANT_TRUE : VARIANT_FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsParamWriteable(RigParamX p, VARIANT_BOOL* value) override
    { *value = (writable & p) ? VARIANT_TRUE : VARIANT_FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Status(RigStatusX* value) override { *value = status; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_StatusStr(BSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Freq(long* value) override
    { *value = staleFrequencyReadback ? 7177000 : frequency; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Freq(long value) override { frequency = value; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FreqA(long* value) override { return get_Freq(value); }
    HRESULT STDMETHODCALLTYPE put_FreqA(long value) override { return put_Freq(value); }
    HRESULT STDMETHODCALLTYPE get_FreqB(long* value) override { return get_Freq(value); }
    HRESULT STDMETHODCALLTYPE put_FreqB(long value) override { return put_Freq(value); }
    HRESULT STDMETHODCALLTYPE get_RitOffset(long*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_RitOffset(long) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Pitch(long*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_Pitch(long) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Vfo(RigParamX* value) override { *value = PM_VFOA; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Vfo(RigParamX) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Split(RigParamX*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_Split(RigParamX) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Rit(RigParamX*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_Rit(RigParamX) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Xit(RigParamX*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_Xit(RigParamX) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Tx(RigParamX* value) override { *value = PM_RX; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Tx(RigParamX) override { ++pttRequests; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Mode(RigParamX* value) override { *value = mode; return readResult; }
    HRESULT STDMETHODCALLTYPE put_Mode(RigParamX value) override
    {
        ++modeRequests;
        if (value == PM_DIG_L)
        {
            ++diglRequests;
            if (diglResult != S_OK) return diglResult;
        }
        if (!ignoreWrites)
        {
            mode = value;
            frequency += 700; // Simulate radios with sideband-specific offsets.
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ClearRit() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetSimplexMode(long) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetSplitMode(long, long) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FrequencyOfTone(long, long*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SendCustomCommand(VARIANT, long, VARIANT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRxFrequency(long* value) override { return get_Freq(value); }
    HRESULT STDMETHODCALLTYPE GetTxFrequency(long* value) override { return get_Freq(value); }
    HRESULT STDMETHODCALLTYPE get_PortBits(IPortBits**) override { return E_NOTIMPL; }
};

struct OmniRigControllerTestAccess
{
    static void run(OmniRigController& controller, const std::function<void()>& fn)
    {
        std::promise<void> done;
        auto future = done.get_future();
        controller.enqueue_([&]() { fn(); done.set_value(); });
        future.wait();
    }
    static void attach(OmniRigController& controller, FakeOmniRig& rig)
    {
        run(controller, [&]() {
            controller.rig_.store(&rig);
            controller.writableParams_ = rig.writable;
            controller.requestCurrentFrequencyModeImpl_();
        });
    }
    static void poll(OmniRigController& controller)
    { run(controller, [&]() { controller.requestCurrentFrequencyModeImpl_(); }); }
    static void detach(OmniRigController& controller)
    { run(controller, [&]() { controller.rig_.store(nullptr); }); }
};

namespace {
using Mode = IRigFrequencyController::Mode;

struct Fixture
{
    FakeOmniRig radio;
    OmniRigController controller;
    explicit Fixture(bool frequencyOnly = false) : controller(0, false, frequencyOnly) {}
    ~Fixture() { OmniRigControllerTestAccess::detach(controller); }
    void attach() { OmniRigControllerTestAccess::attach(controller, radio); }
    void set(Mode mode)
    {
        controller.setMode(mode, true);
        OmniRigControllerTestAccess::poll(controller);
        // Drain requests enqueued by a mode/frequency operation too.
        OmniRigControllerTestAccess::poll(controller);
    }
};

bool fallbackUsesCapabilitiesAndPreservesFrequency()
{
    Fixture f;
    f.radio.writable = PM_SSB_L | PM_SSB_U;
    f.attach();
    f.set(Mode::DIGL);
    return f.radio.diglRequests == 0 && f.radio.mode == PM_SSB_L &&
        f.radio.frequency == 7177000 && f.controller.getConfirmedMode() == Mode::LSB;
}

bool fallbackUsesCommandResultAndIsRemembered()
{
    for (const HRESULT error : {E_NOTIMPL, E_INVALIDARG, DISP_E_MEMBERNOTFOUND})
    {
        Fixture f;
        f.radio.diglResult = error;
        f.attach();
        f.set(Mode::DIGL);
        if (f.radio.mode != PM_SSB_L || f.radio.diglRequests != 1 ||
            f.controller.getConfirmedMode() != Mode::LSB) return false;
        f.set(Mode::USB);
        f.set(Mode::DIGL);
        if (f.radio.diglRequests != 1 || f.radio.frequency != 7177000) return false;
    }
    return true;
}

bool communicationFailureDoesNotFallBack()
{
    Fixture f;
    f.radio.diglResult = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    f.attach();
    f.set(Mode::DIGL);
    return f.radio.modeRequests == 1 && f.radio.mode == PM_SSB_U && f.radio.frequency == 7177000;
}

bool unreadableOfflineOrFailedModesAreUnknown()
{
    for (int failure = 0; failure < 3; ++failure)
    {
        Fixture f;
        if (failure == 0) f.radio.readable = 0;
        if (failure == 1) f.radio.status = ST_NOTRESPONDING;
        if (failure == 2) f.radio.readResult = E_FAIL;
        f.attach();
        f.set(Mode::DIGL);
        if (f.controller.getConfirmedMode() != Mode::UNKNOWN) return false;
    }
    return true;
}

bool acceptedCommandDoesNotConfirmRequestedMode()
{
    Fixture f;
    f.radio.ignoreWrites = true;
    f.attach();
    f.set(Mode::DIGL);
    return f.controller.getConfirmedMode() == Mode::USB && f.radio.mode == PM_SSB_U;
}

bool qsyPreservedWithDelayedReadback()
{
    Fixture f;
    f.attach();
    OmniRigControllerTestAccess::run(f.controller, [&]() { f.radio.staleFrequencyReadback = true; });
    f.controller.setFrequency(14236000);
    f.set(Mode::DIGL);
    return f.radio.frequency == 14236000 && f.radio.mode == PM_DIG_L;
}

bool transmitAndFrequencyOnlyBlockSidebandChanges()
{
    for (const bool frequencyOnly : {false, true})
    {
        Fixture f(frequencyOnly);
        f.attach();
        if (!frequencyOnly) f.controller.setTransmitActive(true);
        f.set(Mode::DIGL);
        f.set(Mode::LSB);
        if (!frequencyOnly)
        {
            f.controller.setFrequency(14236000, true);
            OmniRigControllerTestAccess::poll(f.controller);
        }
        if (f.radio.modeRequests != 0 || f.radio.pttRequests != 0 || f.radio.frequency != 7177000) return false;
    }
    return true;
}
}

int main()
{
    executeTestCase("fallbackUsesCapabilitiesAndPreservesFrequency", fallbackUsesCapabilitiesAndPreservesFrequency);
    executeTestCase("fallbackUsesCommandResultAndIsRemembered", fallbackUsesCommandResultAndIsRemembered);
    executeTestCase("communicationFailureDoesNotFallBack", communicationFailureDoesNotFallBack);
    executeTestCase("unreadableOfflineOrFailedModesAreUnknown", unreadableOfflineOrFailedModesAreUnknown);
    executeTestCase("acceptedCommandDoesNotConfirmRequestedMode", acceptedCommandDoesNotConfirmRequestedMode);
    executeTestCase("qsyPreservedWithDelayedReadback", qsyPreservedWithDelayedReadback);
    executeTestCase("transmitAndFrequencyOnlyBlockSidebandChanges", transmitAndFrequencyOnlyBlockSidebandChanges);
    return testResult();
}
