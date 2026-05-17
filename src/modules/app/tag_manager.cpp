/**
 * @file tag_manager.cpp
 * @brief Tag Manager – Zbus subscriber thread implementation.
 *
 * Subscribes to nfc_events, drives the NFC state machine, and republishes
 * a compact JSON status to ble_nfc_emulator_status for BLE notification.
 */

#include "tag_manager.h"
#include "nfc_uid_utils.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

extern "C" {
#include "zbus_messages.h"
}

LOG_MODULE_REGISTER(tag_manager, CONFIG_LOG_DEFAULT_LEVEL);

extern "C" {
ZBUS_SUBSCRIBER_DEFINE(tag_manager_sub, 8);
ZBUS_CHAN_ADD_OBS(nfc_events, tag_manager_sub, 3);
}

static TagManager manager;

constexpr uint8_t TagManager::TARGET_UID[TagManager::TARGET_UID_LEN];

static const char *state_to_str(nfc_state_t s)
{
	switch (s) {
	case STATE_IDLE:     return "idle";
	case STATE_READING:  return "reading";
	case STATE_VERIFIED: return "verified";
	case STATE_ERROR:    return "error";
	}
	return "unknown";
}

static void publish_status(nfc_state_t state, const uint8_t *uid, uint8_t uid_len)
{
	struct ble_nfc_emulator_status_msg status = {};
	char uid_str[24] = {0};

	ble_nfc_emulator_format_uid(uid, uid_len, uid_str, sizeof(uid_str));

	int written = snprintk(status.payload, sizeof(status.payload),
			       "{\"state\":\"%s\",\"uid\":\"%s\"}",
			       state_to_str(state), uid_str);
	if (written <= 0) {
		return;
	}

	int ret = zbus_chan_pub(&ble_nfc_emulator_status, &status, K_MSEC(50));
	if (ret < 0) {
		LOG_DBG("Status publish skipped: %d", ret);
	}
}

void TagManager::on_tag_event(const uint8_t *tag_id, uint8_t tag_id_len,
			      bool detected, int64_t timestamp_us)
{
	ARG_UNUSED(timestamp_us);

	++event_count_;

	if (!detected) {
		state_ = STATE_IDLE;
		memset(active_uid_, 0, sizeof(active_uid_));
		LOG_INF("Tag REMOVED -> IDLE");
		publish_status(state_, nullptr, 0);
		return;
	}

	state_ = STATE_READING;

	char uid_str[24] = {0};
	ble_nfc_emulator_format_uid(tag_id, tag_id_len, uid_str, sizeof(uid_str));
	LOG_INF("Tag DETECTED [%s] -> READING", uid_str);

#if defined(CONFIG_STRICT_UID_VERIFY)
	const bool verified = (tag_id_len == TARGET_UID_LEN) &&
			      (memcmp(tag_id, TARGET_UID, TARGET_UID_LEN) == 0);
#else
	const bool verified = (tag_id_len > 0U);
#endif

	if (verified) {
		state_ = STATE_VERIFIED;
		uint8_t copy_len = tag_id_len;
		if (copy_len > sizeof(active_uid_)) {
			copy_len = sizeof(active_uid_);
		}
		memcpy(active_uid_, tag_id, copy_len);
		LOG_INF("Verification SUCCESS -> VERIFIED");
	} else {
		state_ = STATE_ERROR;
		LOG_WRN("Verification FAILED -> ERROR");
	}

	publish_status(state_, tag_id, tag_id_len);
}

static void tag_manager_thread(void *, void *, void *)
{
	const struct zbus_channel *chan;
	struct nfc_event_msg msg;

	LOG_INF("Tag Manager thread started — waiting for nfc_events");

	while (true) {
		if (zbus_sub_wait(&tag_manager_sub, &chan, K_FOREVER) != 0) {
			continue;
		}
		if (zbus_chan_read(&nfc_events, &msg, K_MSEC(100)) != 0) {
			LOG_WRN("nfc_events read failed");
			continue;
		}
		manager.on_tag_event(msg.tag_id, msg.tag_id_len,
				     msg.detected, msg.timestamp_us);
	}
}

K_THREAD_DEFINE(tag_manager_tid, 1024,
		tag_manager_thread, NULL, NULL, NULL,
		5, 0, 0);
