/**
 * @file zbus_messages.h
 * @brief Shared Zbus message types and channel declarations for BLE & NFC Emulator.
 *
 * Pure C so it can be included from both .c and .cpp files.
 * All ZBUS_CHAN_DEFINE calls live in zbus_channels.c.
 */

#ifndef ZBUS_MESSAGES_H_
#define ZBUS_MESSAGES_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#define BLE_NFC_EMULATOR_JSON_PAYLOAD_MAX_LEN 80

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*  Message Structs                                                           */
/* -------------------------------------------------------------------------- */

/** NFC tag detection event. */
struct nfc_event_msg {
	uint8_t tag_id[7];     /**< Tag UID (up to 7 bytes, ISO 14443-A) */
	uint8_t tag_id_len;    /**< Actual length of tag_id */
	bool    detected;      /**< true = tag entered field, false = removed */
	int64_t timestamp_us;  /**< Kernel uptime in microseconds */
};

/** Consolidated system status for BLE transmission. */
struct ble_nfc_emulator_status_msg {
	char payload[BLE_NFC_EMULATOR_JSON_PAYLOAD_MAX_LEN]; /**< JSON status payload */
};

/* -------------------------------------------------------------------------- */
/*  Zbus Channel Declarations                                                 */
/* -------------------------------------------------------------------------- */

ZBUS_CHAN_DECLARE(nfc_events);
ZBUS_CHAN_DECLARE(ble_nfc_emulator_status);

#ifdef __cplusplus
}
#endif

#endif /* ZBUS_MESSAGES_H_ */
