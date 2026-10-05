// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <initializer_list>
#include "signal_format.h"

int main() {
    using hisi_signal::Signal;
    // Captured C00 standard response and private indication for the same LTE
    // measurement. The private BER word is deliberately garbage in the blob.
    int32_t query[14] = {0, -1, -1, -1, -1, -1, -1, -1, -102, -15, -9, -1, -1, -1};
    int32_t unsolicited[16] = {0, 813759472, INT_MAX, 255, -1, -1, -1, -1,
                              -1, -1, -102, -15, -9, -1, INT_MAX, 8};
    Signal a, b;
    assert(hisi_signal::from_standard(query, sizeof(query), a));
    assert(hisi_signal::from_private(unsolicited, sizeof(unsolicited), b));
    assert(a.values[8] == 102 && a.values[9] == 15 && a.values[10] == -90);
    assert(b.values[8] == 102 && b.values[9] == 15 && b.values[10] == -90);
    assert(query[8] == -102 && unsolicited[10] == -102); // input is not modified
    assert(a.values[1] == -1 && a.values[13] == -1); // non-LTE query preserved
    assert(b.values[1] == INT_MAX && b.values[13] == INT_MAX);
    assert(b.values[7] == 99); // HIDL unknown LTE RSSI ASU, not INT_MAX
    query[8] = 0; query[9] = 0; query[10] = 0; // observed non-LTE transition
    assert(hisi_signal::from_standard(query, sizeof(query), a));
    assert(a.values[8] == INT_MAX && a.values[9] == INT_MAX && a.values[10] == INT_MAX);
    for (int unavailable : {INT_MAX, -1, 99, -141}) {
        unsolicited[10] = unavailable;
        assert(hisi_signal::from_private(unsolicited, sizeof(unsolicited), b));
        assert(b.values[7] == 99);
        assert(b.values[8] == INT_MAX && b.values[9] == INT_MAX && b.values[10] == INT_MAX);
    }
    unsolicited[10] = -140; unsolicited[11] = -34; unsolicited[12] = -20;
    assert(hisi_signal::from_private(unsolicited, sizeof(unsolicited), b));
    assert(b.values[8] == 140 && b.values[9] == 34 && b.values[10] == -200);
    unsolicited[10] = -44; unsolicited[11] = -3; unsolicited[12] = 30;
    assert(hisi_signal::from_private(unsolicited, sizeof(unsolicited), b));
    assert(b.values[8] == 44 && b.values[9] == 3 && b.values[10] == 300);
    unsolicited[12] = 99; // vendor unavailable SINR
    assert(hisi_signal::from_private(unsolicited, sizeof(unsolicited), b));
    assert(b.values[10] == INT_MAX);
    unsolicited[12] = -1; // -1 dB is a real SINR, not an unavailable measurement
    assert(hisi_signal::from_private(unsolicited, sizeof(unsolicited), b));
    assert(b.values[10] == -10);
    assert(!hisi_signal::from_standard(nullptr, 56, a));
    assert(!hisi_signal::from_standard(query, 52, a));
    assert(!hisi_signal::from_standard(query, 104, a));
    assert(!hisi_signal::from_private(nullptr, 64, b));
    assert(!hisi_signal::from_private(unsolicited, 60, b));
    assert(!hisi_signal::from_private(unsolicited, 76, b));
}
