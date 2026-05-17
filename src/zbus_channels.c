/**
 * @file zbus_channels.c
 * @brief Zbus channel definitions for BLE & NFC Emulator.
 *
 * This file owns the ZBUS_CHAN_DEFINE calls.  It is intentionally kept as
 * plain C because the Zephyr Zbus macros use designated-initialiser syntax
 * that is only valid in C (not C++11).
 */

#include "zbus_messages.h"

/* -------------------------------------------------------------------------- */
/*  Channel Definitions                                                       */
/* -------------------------------------------------------------------------- */

/**
 * nfc_events channel — carries NFC tag detection events.
 * Initial value is all-zeros (no tag detected).
 */
ZBUS_CHAN_DEFINE(nfc_events,               /* channel name        */
		struct nfc_event_msg,      /* message type        */
		NULL,                      /* validator (none)    */
		NULL,                      /* user data (none)    */
		ZBUS_OBSERVERS_EMPTY,      /* no static observers */
		ZBUS_MSG_INIT(.tag_id = {0}, .tag_id_len = 0,
			      .detected = false, .timestamp_us = 0)
);

/**
 * ble_nfc_emulator_status channel — consolidated status for BLE transmission.
 * Updated by the Data Aggregator at ~1 Hz.
 */
ZBUS_CHAN_DEFINE(ble_nfc_emulator_status,           /* channel name        */
		struct ble_nfc_emulator_status_msg, /* message type       */
		NULL,                      /* validator (none)    */
		NULL,                      /* user data (none)    */
		ZBUS_OBSERVERS_EMPTY,      /* no static observers */
		ZBUS_MSG_INIT(.payload = {0})
);
