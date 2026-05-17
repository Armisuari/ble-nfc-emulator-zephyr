/**
 * @file zbus_messages.h
 * @brief Shared Zbus message types and channel declarations for BLE & NFC Emulator.
 *
 * This header is pure C so it can be included from both .c and .cpp files.
 * All Zbus channel definitions live in zbus_channels.c.
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

/** NFC tag detection event (PN7150 / mock). */
struct nfc_event_msg {
	uint8_t tag_id[7];     /**< Tag UID (up to 7 bytes, ISO 14443) */
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

ZBUS_CHAN_DECLARE(nfc_events, ble_nfc_emulator_status_msg, ZBUS_CHAN_PUB_ONLY);

#ifdef __cplusplus
}
#endif

#endif /* ZBUS_MESSAGES_H_ */
