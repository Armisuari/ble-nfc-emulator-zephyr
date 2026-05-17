#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/printk.h>

static inline void ble_nfc_emulator_format_uid(const uint8_t *uid,
                                       uint8_t uid_len,
                                       char *out,
                                       size_t out_len)
{
    if (out == nullptr || out_len == 0U) {
        return;
    }

    out[0] = '\0';

    if (uid == nullptr || uid_len == 0U) {
        (void)snprintk(out, out_len, "--");
        return;
    }

    size_t used = 0U;
    for (uint8_t i = 0; i < uid_len && i < 7U; ++i) {
        if (i > 0U && used + 1U < out_len) {
            out[used++] = ':';
            out[used] = '\0';
        }

        if (used + 2U >= out_len) {
            break;
        }

        int written = snprintk(&out[used], out_len - used, "%02X", uid[i]);
        if (written < 0) {
            break;
        }
        used += (size_t)written;
    }
}
