// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace hisi_signal {
// CMR-AL09 C00 9.1.0.329 libril consumes the 56-byte RIL v6 layout.
// Do not use the newer, larger RIL_SignalStrength structure from ril.h.
struct Signal {
    int32_t values[14];
};
static_assert(sizeof(Signal) == 56);

inline bool in_range(int32_t value, int32_t low, int32_t high) {
    return value >= low && value <= high;
}

inline void convert_lte(Signal& signal) {
    auto& v = signal.values;
    // Both C00 query and private indication contain signed dBm/dB. HIDL 1.0
    // needs RSRP/RSRQ magnitudes and SINR in tenths of a dB (RILUtils.java).
    const bool has_lte = in_range(v[8], -140, -44);
    v[8] = has_lte ? -v[8] : INT_MAX;
    v[9] = has_lte && in_range(v[9], -34, -3) ? -v[9] : INT_MAX;
    v[10] = has_lte && in_range(v[10], -20, 30) ? v[10] * 10 : INT_MAX;
}

inline bool from_standard(const void* data, size_t size, Signal& signal) {
    if (!data || size != sizeof(signal)) return false;
    memcpy(&signal, data, sizeof(signal));
    // Preserve the existing vendor treatment of all non-LTE fields.
    convert_lte(signal);
    return true;
}

inline bool from_private(const void* data, size_t size, Signal& signal) {
    if (!data || size != 16 * sizeof(int32_t)) return false;
    int32_t raw[16];
    memcpy(raw, data, sizeof(raw));
    for (auto& value : signal.values) value = INT_MAX;
    // HIDL LTE RSSI uses ASU 99 for unknown, unlike the INT_MAX sentinel of
    // RSRP/RSRQ/SINR. INT_MAX here produces an invalid-RSSI log on every update.
    signal.values[7] = 99;
    // Only LTE has been validated. In particular raw[1] is uninitialized
    // vendor memory, and raw[15] is the band, not standard TD-SCDMA RSCP.
    signal.values[8] = raw[10];
    signal.values[9] = raw[11];
    signal.values[10] = raw[12];
    convert_lte(signal);
    return true;
}
}  // namespace hisi_signal
