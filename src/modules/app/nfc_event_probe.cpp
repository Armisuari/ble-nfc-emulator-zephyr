/**
 * @file nfc_event_probe.cpp
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "nfc_uid_utils.h"

extern "C" {
#include "zbus_messages.h"
}

LOG_MODULE_REGISTER(nfc_event_probe, CONFIG_LOG_DEFAULT_LEVEL);

extern "C" {
ZBUS_SUBSCRIBER_DEFINE(nfc_probe_sub, 8);
ZBUS_CHAN_ADD_OBS(nfc_events, nfc_probe_sub, 2);
}

static void nfc_probe_thread(void *, void *, void *)
{
    int64_t last_timestamp_us = -1;
    const struct zbus_channel *chan = nullptr;

    LOG_INF("NFC probe active - waiting for nfc_events");

    while (true) {
        if (zbus_sub_wait(&nfc_probe_sub, &chan, K_FOREVER) != 0) {
            continue;
        }

        struct nfc_event_msg msg = {};
        if (zbus_chan_read(&nfc_events, &msg, K_MSEC(100)) != 0) {
            LOG_WRN("NFC probe failed to read nfc_events");
            continue;
        }

        if (msg.tag_id_len > sizeof(msg.tag_id)) {
            LOG_WRN("NFC probe got invalid tag_id_len=%u", msg.tag_id_len);
            continue;
        }

        char uid_str[32] = {0};
        ble_nfc_emulator_format_uid(msg.tag_id, msg.tag_id_len, uid_str, sizeof(uid_str));

        int64_t delta_us = 0;
        if (last_timestamp_us >= 0) {
            delta_us = msg.timestamp_us - last_timestamp_us;
        }

        last_timestamp_us = msg.timestamp_us;

        LOG_INF("NFC EVT | det=%d len=%u dt_us=%lld uid=%s",
                msg.detected ? 1 : 0,
                msg.tag_id_len,
                static_cast<long long>(delta_us),
                uid_str);
    }
}

K_THREAD_DEFINE(nfc_probe_tid, 1024,
                nfc_probe_thread, nullptr, nullptr, nullptr,
                7, 0, 0);
