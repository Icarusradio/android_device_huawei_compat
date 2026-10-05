// SPDX-License-Identifier: Apache-2.0
#include <dlfcn.h>
#include <log/log.h>
#include <stdlib.h>
#include <telephony/ril.h>

#include "signal_format.h"

#define TAG "HisiSignalBridge"
namespace {
using Handler = int (*)(int, int, int, RIL_Errno, void*, size_t);
Handler real_hw;
Handler real_query;
Handler real_indication;

template <typename T> T next(const char* name) {
    auto fn = reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
    if (!fn) {
        __android_log_print(ANDROID_LOG_FATAL, TAG, "Missing original %s: %s", name, dlerror());
        abort();
    }
    return fn;
}

__attribute__((constructor)) void initialize() {
    real_hw = next<Handler>("_ZN5radio27currentHwSignalStrength_1_1Eiii9RIL_ErrnoPvm");
    real_query = next<Handler>("_ZN5radio25getSignalStrengthResponseEiii9RIL_ErrnoPvm");
    real_indication = next<Handler>("_ZN5radio24currentSignalStrengthIndEiii9RIL_ErrnoPvm");
    __android_log_print(ANDROID_LOG_INFO, TAG, "C00 LTE signal bridge loaded");
}

int standard(Handler handler, int slot, int type, int serial, RIL_Errno error,
             void* data, size_t size) {
    hisi_signal::Signal signal;
    if (error == RIL_E_SUCCESS && hisi_signal::from_standard(data, size, signal)) {
        return handler(slot, type, serial, error, &signal, sizeof(signal));
    }
    return handler(slot, type, serial, error, data, size);
}
}  // namespace

namespace radio {
// C00 libril calls these exported handlers through its PLT. Keep the original
// serial, slot, indication type and RIL error; the blob owns binder/ACK handling.
int currentHwSignalStrength_1_1(int slot, int type, int serial, RIL_Errno error,
                               void* data, size_t size) {
    hisi_signal::Signal signal;
    if (error == RIL_E_SUCCESS && hisi_signal::from_private(data, size, signal)) {
        real_indication(slot, type, serial, error, &signal, sizeof(signal));
    }
    // Preserve the private path for any vendor client. It may mutate its input,
    // so conversion above uses a separate, checked copy.
    return real_hw(slot, type, serial, error, data, size);
}

int getSignalStrengthResponse(int slot, int type, int serial, RIL_Errno error,
                              void* data, size_t size) {
    return standard(real_query, slot, type, serial, error, data, size);
}

int currentSignalStrengthInd(int slot, int type, int serial, RIL_Errno error,
                            void* data, size_t size) {
    return standard(real_indication, slot, type, serial, error, data, size);
}
}  // namespace radio
