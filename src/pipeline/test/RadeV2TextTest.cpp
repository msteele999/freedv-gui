#include <cstring>
#include <string>
#include <vector>

#include "../radev2/rade_v2_text.h"
#include "../../test/UnitTestCommon.h"

namespace
{
struct Receiver
{
    rade_v2_text_t text = rade_v2_text_create();
    std::string received;
    int callbacks = 0;

    Receiver()
    {
        rade_v2_text_enable_stats_output(text, false);
        rade_v2_text_set_rx_callback(text, [](rade_v2_text_t, const char* value, int size, void* state) {
            auto& receiver = *static_cast<Receiver*>(state);
            receiver.received.assign(value, size);
            ++receiver.callbacks;
        }, this);
    }

    ~Receiver() { rade_v2_text_destroy(text); }
};

bool streamingCompoundCallsignAndIndependentReceivers()
{
    auto tx = rade_v2_text_create();
    const char* callsign = "VE3/KG6AOV/MM";
    rade_v2_text_enable_stats_output(tx, false);
    rade_v2_text_generate_tx_string(tx, callsign, std::strlen(callsign));
    Receiver first;
    Receiver second;
    // Enough symbols for repeated two-block cycles, starting at different
    // offsets. The decoder must own its state independently for each receiver.
    for (int index = 0; index < 1600; ++index)
    {
        const float symbol = rade_v2_text_tx_next_symbol(tx);
        rade_v2_text_rx_symbol(first.text, symbol);
        if (index >= 73) rade_v2_text_rx_symbol(second.text, symbol);
    }
    bool result = first.callbacks > 0 && second.callbacks > 0 &&
        first.received == callsign && second.received == callsign;
    rade_v2_text_rx_reset(second.text);
    second.received.clear();
    second.callbacks = 0;
    for (int index = 0; index < 1600; ++index)
        rade_v2_text_rx_symbol(second.text, rade_v2_text_tx_next_symbol(tx));
    result &= second.callbacks > 0 && second.received == callsign;
    rade_v2_text_destroy(tx);
    return result;
}

bool incompleteStreamDoesNotPublishCallsign()
{
    auto tx = rade_v2_text_create();
    rade_v2_text_generate_tx_string(tx, "K6AQ", 4);
    Receiver receiver;
    for (int index = 0; index < 50; ++index)
        rade_v2_text_rx_symbol(receiver.text, rade_v2_text_tx_next_symbol(tx));
    rade_v2_text_rx_reset(receiver.text);
    const bool result = receiver.callbacks == 0;
    rade_v2_text_destroy(tx);
    return result;
}
}

int main()
{
    executeTestCase("streamingCompoundCallsignAndIndependentReceivers", streamingCompoundCallsignAndIndependentReceivers);
    executeTestCase("incompleteStreamDoesNotPublishCallsign", incompleteStreamDoesNotPublishCallsign);
    return testResult();
}
