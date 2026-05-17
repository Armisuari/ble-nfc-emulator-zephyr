/**
 * @file tag_manager.cpp
 * @brief Tag Manager – Zbus subscriber thread implementation.
 *
 * Bridges the TagManager C++ class with the C-based Zephyr Zbus API.
 * A dedicated thread waits for nfc_events notifications and delegates
 * processing to the TagManager instance.
 */

#include "tag_manager.h"
#include "nfc_uid_utils.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

extern "C" {
#include "zbus_messages.h"
}

LOG_MODULE_REGISTER(tag_manager, CONFIG_LOG_DEFAULT_LEVEL);

/* ---- Zbus subscriber (C linkage) ---------------------------------------- */
extern "C" {
ZBUS_SUBSCRIBER_DEFINE(tag_manager_sub, 8);
ZBUS_CHAN_ADD_OBS(nfc_events, tag_manager_sub, 3);
}

/* ---- Singleton instance -------------------------------------------------- */
static TagManager manager;

/* ---- TagManager methods -------------------------------------------------- */
constexpr uint8_t TagManager::TARGET_UID[4];

void TagManager::on_tag_event(const uint8_t *tag_id, uint8_t tag_id_len,
			      bool detected, int64_t timestamp_us)
{
	ARG_UNUSED(timestamp_us);

	++event_count_;

	if (!detected) {
		/* Tag Removed Event */
		state_ = STATE_IDLE;
		memset(active_uid_, 0, sizeof(active_uid_));
		LOG_INF("NFC State Trigger | Tag REMOVED -> Transitioning to IDLE");
		return;
	}

	/* Tag Detected Event -> Transition to READING */
	state_ = STATE_READING;

	char uid_str[32] = {0};
	smarttag_format_uid(tag_id, tag_id_len, uid_str, sizeof(uid_str));
	LOG_INF("NFC State Trigger | Tag DETECTED [%s] -> Transitioning to READING", uid_str);

	/* Verification Step */
	bool verified = false;
#if defined(CONFIG_SMARTTAG_TAG_STRICT_UID_VERIFY)
	if (tag_id_len == TARGET_UID_LEN && memcmp(tag_id, TARGET_UID, TARGET_UID_LEN) == 0) {
		verified = true;
	}
#else
	/* PN532 bring-up mode: accept any non-empty UID as verified. */
	verified = (tag_id_len > 0U);
#endif

	if (verified) {
		state_ = STATE_VERIFIED;
		uint8_t copy_len = tag_id_len;
		if (copy_len > sizeof(active_uid_)) {
			copy_len = sizeof(active_uid_);
		}
		memcpy(active_uid_, tag_id, copy_len);
		LOG_INF("NFC State         | Verification SUCCESS -> Transitioning to VERIFIED");
	} else {
		state_ = STATE_ERROR;
		LOG_WRN("NFC State         | Verification FAILED -> Transitioning to ERROR");
	}
}

/* ---- Thread entry point -------------------------------------------------- */
static void tag_manager_thread(void *, void *, void *)
{
	const struct zbus_channel *chan;

	LOG_INF("Tag Manager thread started — waiting for nfc_events");

	while (true) {
		if (zbus_sub_wait(&tag_manager_sub, &chan, K_FOREVER) == 0) {
			struct nfc_event_msg msg;

			if (zbus_chan_read(&nfc_events, &msg,
					  K_MSEC(100)) == 0) {
				manager.on_tag_event(msg.tag_id,
						     msg.tag_id_len,
						     msg.detected,
						     msg.timestamp_us);
			}
		}
	}
}

K_THREAD_DEFINE(tag_manager_tid, 1024,
		tag_manager_thread, NULL, NULL, NULL,
		5, 0, 0);
