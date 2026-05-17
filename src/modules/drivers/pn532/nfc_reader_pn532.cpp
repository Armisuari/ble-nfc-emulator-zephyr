/**
 * @file nfc_reader_pn532.cpp
 * @brief PN532 NFC reader publisher for BLE & NFC Emulator Zbus pipeline.
 *
 * This module is selected via CONFIG_NFC_PN532 and publishes
 * `nfc_event_msg` into `nfc_events` to keep app/middleware unchanged.
 */

#include "nfc_reader_pn532.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_I2C)
#include <zephyr/drivers/i2c.h>
#endif

extern "C" {
#include "zbus_messages.h"
}

#if defined(CONFIG_I2C_SCANNER_ON_BOOT)
#include "modules/app/i2c_scanner.h"
#endif

LOG_MODULE_REGISTER(nfc_reader_pn532, CONFIG_LOG_DEFAULT_LEVEL);

#if defined(CONFIG_I2C)

#define PN532_NODE DT_ALIAS(pn532)
#if !DT_NODE_HAS_STATUS(PN532_NODE, okay)
#error "PN532 alias is not defined in devicetree (alias: pn532)"
#endif

static const struct i2c_dt_spec pn532_i2c = I2C_DT_SPEC_GET(PN532_NODE);

/* PN532 over I2C (HSU framing not used here). */
static constexpr uint8_t PN532_CMD_GET_FIRMWARE_VERSION = 0x02;
static constexpr uint8_t PN532_CMD_SAM_CONFIGURATION    = 0x14;
static constexpr uint8_t PN532_CMD_IN_LIST_PASSIVE      = 0x4A;
static constexpr uint8_t PN532_RSP_IN_LIST_PASSIVE      = 0x4B;
static constexpr uint8_t PN532_RSP_GET_FIRMWARE_VERSION = 0x03;
static constexpr uint8_t PN532_RSP_SAM_CONFIGURATION    = 0x15;
static constexpr uint8_t PN532_PREAMBLE                 = 0x00;
static constexpr uint8_t PN532_STARTCODE1               = 0x00;
static constexpr uint8_t PN532_STARTCODE2               = 0xFF;
static constexpr uint8_t PN532_HOST_TO_PN532            = 0xD4;
static constexpr uint8_t PN532_PN532_TO_HOST            = 0xD5;
static constexpr uint8_t PN532_I2C_READY                = 0x01;

static constexpr size_t PN532_FRAME_MAX = 32;
static constexpr size_t PN532_READ_MAX  = 40;

/* Fallbacks if Kconfig values are absent (e.g. driver-only build). */
#if !defined(CONFIG_PN532_DETECT_STREAK)
#define CONFIG_PN532_DETECT_STREAK 2
#endif
#if !defined(CONFIG_PN532_REMOVE_STREAK)
#define CONFIG_PN532_REMOVE_STREAK 3
#endif
#if !defined(CONFIG_PN532_PUBLISH_TIMEOUT_MS)
#define CONFIG_PN532_PUBLISH_TIMEOUT_MS 100
#endif
#if !defined(CONFIG_PN532_POLL_PERIOD_MS)
#define CONFIG_PN532_POLL_PERIOD_MS 120
#endif

NfcReaderPn532::NfcReaderPn532()
    : i2c_bus_(nullptr),
      ready_(false),
      running_(false),
      last_detected_(false),
    last_uid_{0},
    last_uid_len_(0),
    detect_streak_(0),
    miss_streak_(0),
      last_error_(0)
{
}

static int pn532_write_command(uint8_t command,
                               const uint8_t *payload,
                               uint8_t payload_len)
{
    uint8_t frame[PN532_FRAME_MAX] = {0};
    uint8_t data_len = static_cast<uint8_t>(2U + payload_len); /* TFI + CMD + payload */

    /* Standard PN532 frame with checksum bytes. */
    size_t idx = 0;
    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_STARTCODE1;
    frame[idx++] = PN532_STARTCODE2;
    frame[idx++] = data_len;
    frame[idx++] = static_cast<uint8_t>(~data_len + 1U);
    frame[idx++] = PN532_HOST_TO_PN532;
    frame[idx++] = command;

    uint8_t dcs = static_cast<uint8_t>(PN532_HOST_TO_PN532 + command);
    for (uint8_t i = 0; i < payload_len; ++i) {
        frame[idx++] = payload[i];
        dcs = static_cast<uint8_t>(dcs + payload[i]);
    }
    frame[idx++] = static_cast<uint8_t>(~dcs + 1U);
    frame[idx++] = 0x00; /* Postamble */

    return i2c_write_dt(&pn532_i2c, frame, idx);
}

static int pn532_read_bytes(uint8_t *buf, size_t len)
{
    return i2c_read_dt(&pn532_i2c, buf, len);
}

static int pn532_wait_ready(uint32_t timeout_ms)
{
    int64_t deadline = k_uptime_get() + timeout_ms;

    while (k_uptime_get() < deadline) {
        uint8_t status = 0;
        int ret = pn532_read_bytes(&status, sizeof(status));
        if (ret == 0 && status == PN532_I2C_READY) {
            return 0;
        }
        k_msleep(5);
    }

    return -ETIMEDOUT;
}

static int pn532_read_frame(uint8_t *buf, size_t len)
{
    if (buf == nullptr || len == 0U || (len + 1U) > PN532_READ_MAX) {
        return -EINVAL;
    }

    uint8_t raw[PN532_READ_MAX] = {0};
    int ret = pn532_read_bytes(raw, len + 1U);
    if (ret != 0) {
        return ret;
    }

    if (raw[0] != PN532_I2C_READY) {
        return -EAGAIN;
    }

    memcpy(buf, &raw[1], len);
    return 0;
}

static int pn532_read_ack(uint32_t timeout_ms)
{
    static const uint8_t ack_expected[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    uint8_t ack[6] = {0};

    int ret = pn532_wait_ready(timeout_ms);
    if (ret != 0) {
        return ret;
    }

    ret = pn532_read_frame(ack, sizeof(ack));
    if (ret != 0) {
        return ret;
    }

    if (memcmp(ack, ack_expected, sizeof(ack_expected)) != 0) {
        return -EIO;
    }

    return 0;
}

static int pn532_send_command(uint8_t command,
                              const uint8_t *payload,
                              uint8_t payload_len)
{
    int ret = pn532_write_command(command, payload, payload_len);
    if (ret != 0) {
        return ret;
    }

    return pn532_read_ack(100);
}

static bool pn532_response_has_cmd(const uint8_t *resp, size_t resp_len, uint8_t cmd)
{
    for (size_t i = 0; i + 1U < resp_len; ++i) {
        if (resp[i] == PN532_PN532_TO_HOST && resp[i + 1U] == cmd) {
            return true;
        }
    }
    return false;
}

static int pn532_parse_in_list_passive_uid(const uint8_t *resp,
                                           size_t resp_len,
                                           uint8_t *uid,
                                           uint8_t *uid_len)
{
    /*
     * Parse data section of InListPassiveTarget response:
     *   D5 4B NbTg Tg SENS_RES[2] SEL_RES NFCIDLen NFCID...
     */
    for (size_t i = 0; i + 8U < resp_len; ++i) {
        if (resp[i] != PN532_PN532_TO_HOST || resp[i + 1U] != PN532_RSP_IN_LIST_PASSIVE) {
            continue;
        }

        uint8_t nb_tg = resp[i + 2U];
        if (nb_tg == 0U) {
            return -ENODATA;
        }

        uint8_t parsed_uid_len = resp[i + 7U];
        if (parsed_uid_len < 4U || parsed_uid_len > 7U) {
            return -ENODATA;
        }

        size_t uid_start = i + 8U;
        if (uid_start + parsed_uid_len > resp_len) {
            return -ENODATA;
        }

        memcpy(uid, &resp[uid_start], parsed_uid_len);
        *uid_len = parsed_uid_len;
        return 0;
    }

    return -ENODATA;
}

int NfcReaderPn532::init()
{
    if (!device_is_ready(pn532_i2c.bus)) {
        last_error_ = -ENODEV;
        LOG_ERR("PN532 I2C bus not ready");
        return last_error_;
    }

#if defined(CONFIG_I2C_SCANNER_ON_BOOT)
    int scan_found = i2c_scanner_run_once();
    if (scan_found <= 0) {
        last_error_ = -ENODEV;
        LOG_WRN("PN532 init skipped because scanner found no I2C responder");
        ready_ = false;
        return last_error_;
    }
#endif

    i2c_bus_ = pn532_i2c.bus;

    /* Quick sanity command so wiring/protocol issues fail early in logs. */
    last_error_ = pn532_send_command(PN532_CMD_GET_FIRMWARE_VERSION, nullptr, 0);
    if (last_error_ != 0) {
        LOG_WRN("PN532 firmware command write failed: %d (i2c_addr=0x%02X)",
                last_error_, pn532_i2c.addr);
        ready_ = false;
        return last_error_;
    }

    last_error_ = pn532_wait_ready(120);
    if (last_error_ != 0) {
        LOG_WRN("PN532 firmware response timeout: %d", last_error_);
        ready_ = false;
        return last_error_;
    }

    uint8_t fw_resp[24] = {0};
    last_error_ = pn532_read_frame(fw_resp, sizeof(fw_resp));
    if (last_error_ != 0) {
        LOG_WRN("PN532 firmware response read failed: %d", last_error_);
        ready_ = false;
        return last_error_;
    }

    if (!pn532_response_has_cmd(fw_resp, sizeof(fw_resp), PN532_RSP_GET_FIRMWARE_VERSION)) {
        LOG_WRN("PN532 firmware response invalid");
        ready_ = false;
        return -EIO;
    }

    /* Enter normal mode SAM configuration (required for tag polling). */
    const uint8_t sam_payload[] = {0x01, 0x14, 0x01};
    last_error_ = pn532_send_command(PN532_CMD_SAM_CONFIGURATION,
                                     sam_payload,
                                     sizeof(sam_payload));
    if (last_error_ != 0) {
        LOG_WRN("PN532 SAM config write failed: %d", last_error_);
        ready_ = false;
        return last_error_;
    }

    last_error_ = pn532_wait_ready(120);
    if (last_error_ != 0) {
        LOG_WRN("PN532 SAM config response timeout: %d", last_error_);
        ready_ = false;
        return last_error_;
    }

    uint8_t sam_resp[24] = {0};
    last_error_ = pn532_read_frame(sam_resp, sizeof(sam_resp));
    if (last_error_ != 0) {
        LOG_WRN("PN532 SAM config response read failed: %d", last_error_);
        ready_ = false;
        return last_error_;
    }

    if (!pn532_response_has_cmd(sam_resp, sizeof(sam_resp), PN532_RSP_SAM_CONFIGURATION)) {
        LOG_WRN("PN532 SAM config response invalid");
        ready_ = false;
        return -EIO;
    }

    ready_ = true;
    LOG_INF("PN532 reader initialized on I2C bus: %s", i2c_bus_->name);
    return 0;
}

int NfcReaderPn532::start()
{
    if (!ready_) {
        last_error_ = -EAGAIN;
        return last_error_;
    }

    running_ = true;
    return 0;
}

void NfcReaderPn532::stop()
{
    running_ = false;
}

bool NfcReaderPn532::is_ready() const
{
    return ready_;
}

int NfcReaderPn532::last_error() const
{
    return last_error_;
}

int NfcReaderPn532::publish_nfc_event(const uint8_t *uid, uint8_t uid_len, bool detected)
{
    struct nfc_event_msg msg = {};

    if (uid_len > sizeof(msg.tag_id)) {
        uid_len = sizeof(msg.tag_id);
    }

    if (uid != nullptr && uid_len > 0U) {
        memcpy(msg.tag_id, uid, uid_len);
    }

    msg.tag_id_len = uid_len;
    msg.detected = detected;
    msg.timestamp_us = k_ticks_to_us_floor64(k_uptime_ticks());

    int ret = zbus_chan_pub(&nfc_events, &msg,
                            K_MSEC(CONFIG_PN532_PUBLISH_TIMEOUT_MS));
    if (ret < 0) {
        LOG_WRN("Failed to publish nfc_events: %d", ret);
    }

    return ret;
}

int NfcReaderPn532::read_passive_target_uid(uint8_t *uid, uint8_t *uid_len)
{
    if (uid == nullptr || uid_len == nullptr) {
        return -EINVAL;
    }

    const uint8_t poll_payload[] = {
        0x01, /* max targets */
        0x00  /* 106 kbps type A */
    };

    int ret = pn532_send_command(PN532_CMD_IN_LIST_PASSIVE,
                                 poll_payload,
                                 sizeof(poll_payload));
    if (ret != 0) {
        return ret;
    }

    ret = pn532_wait_ready(80);
    if (ret != 0) {
        return ret;
    }

    uint8_t resp[24] = {0};
    ret = pn532_read_frame(resp, sizeof(resp));
    if (ret != 0) {
        return ret;
    }

    return pn532_parse_in_list_passive_uid(resp, sizeof(resp), uid, uid_len);
}

void NfcReaderPn532::process_once()
{
    if (!running_) {
        return;
    }

    uint8_t uid[7] = {0};
    uint8_t uid_len = 0;

    int ret = read_passive_target_uid(uid, &uid_len);
    if (ret == 0) {
        miss_streak_ = 0;

        bool same_uid = (uid_len == last_uid_len_) &&
                        (uid_len > 0U) &&
                        (memcmp(uid, last_uid_, uid_len) == 0);

        if (!same_uid) {
            memset(last_uid_, 0, sizeof(last_uid_));
            if (uid_len > 0U) {
                memcpy(last_uid_, uid, uid_len);
            }
            last_uid_len_ = uid_len;
            detect_streak_ = 1U;
        } else if (detect_streak_ < UINT8_MAX) {
            ++detect_streak_;
        }

        if (!last_detected_ && detect_streak_ >= CONFIG_PN532_DETECT_STREAK) {
            publish_nfc_event(uid, uid_len, true);
            last_detected_ = true;
            LOG_INF("PN532 tag detected (uid_len=%u)", uid_len);
        }
        return;
    }

    if (ret == -ENODATA || ret == -EIO || ret == -ETIMEDOUT || ret == -EBUSY) {
        detect_streak_ = 0;

        if (last_detected_) {
            if (miss_streak_ < UINT8_MAX) {
                ++miss_streak_;
            }

            if (miss_streak_ >= CONFIG_PN532_REMOVE_STREAK) {
                publish_nfc_event(nullptr, 0, false);
                last_detected_ = false;
                miss_streak_ = 0;
                last_uid_len_ = 0;
                LOG_INF("PN532 tag removed");
            }
        } else {
            miss_streak_ = 0;
        }
        return;
    }

    last_error_ = ret;
    LOG_WRN("PN532 poll/read failed: %d", ret);
}

static NfcReaderPn532 g_pn532_reader;

static void pn532_poll_thread(void *, void *, void *)
{
    while (true) {
        if (g_pn532_reader.is_ready()) {
            g_pn532_reader.process_once();
        }

        k_msleep(CONFIG_PN532_POLL_PERIOD_MS);
    }
}

K_THREAD_DEFINE(pn532_poll_tid, 1536,
                pn532_poll_thread, nullptr, nullptr, nullptr,
                6, 0, 0);

static int nfc_reader_pn532_init(void)
{
    int ret = g_pn532_reader.init();
    if (ret != 0) {
        LOG_WRN("PN532 init failed: %d", ret);
        return 0;
    }

    ret = g_pn532_reader.start();
    if (ret != 0) {
        LOG_WRN("PN532 start failed: %d", ret);
        return 0;
    }

    LOG_INF("PN532 NFC publisher started");
    return 0;
}

SYS_INIT(nfc_reader_pn532_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#else

static int nfc_reader_pn532_init(void)
{
    LOG_ERR("CONFIG_I2C is required for PN532 mode");
    return 0;
}

SYS_INIT(nfc_reader_pn532_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_I2C */
