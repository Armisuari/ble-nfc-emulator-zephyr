/**
 * @file tag_manager.h
 * @brief Tag Manager module — subscribes to the nfc_events Zbus channel.
 */

#ifndef TAG_MANAGER_H_
#define TAG_MANAGER_H_

#include <cstdint>
#include <string.h>

/**
 * @enum nfc_state_t
 * @brief Represents the current state of the NFC lifecycle.
 */
typedef enum {
	STATE_IDLE,
	STATE_READING,
	STATE_VERIFIED,
	STATE_ERROR
} nfc_state_t;

/**
 * @class TagManager
 * @brief Processes NFC tag detection events and manages the verification state machine.
 */
class TagManager {
public:
	/* The target bit ID the system accepts in this PoC phase */
	static constexpr uint8_t TARGET_UID_LEN = 4;
	static constexpr uint8_t TARGET_UID[TARGET_UID_LEN] = {0x04, 0xBA, 0xDF, 0x00};

	TagManager() 
		: state_(STATE_IDLE)
		, event_count_(0)
	{
		memset(active_uid_, 0, sizeof(active_uid_));
	}

	/**
	 * Handle a tag detection/removal event.
	 * @param tag_id       Pointer to tag UID bytes
	 * @param tag_id_len   Length of the UID
	 * @param detected     true if tag entered the field
	 * @param timestamp_us Kernel uptime in microseconds
	 */
	void on_tag_event(const uint8_t *tag_id, uint8_t tag_id_len,
			  bool detected, int64_t timestamp_us);

	/** Return current state of the NFC manager. */
	nfc_state_t get_state() const { return state_; }

	/** Return total number of events processed. */
	uint32_t get_event_count() const { return event_count_; }

	/** Return the active verified UID (only valid if state is STATE_VERIFIED). */
	const uint8_t* get_active_uid() const { return active_uid_; }

private:
	nfc_state_t state_;
	uint32_t    event_count_;
	uint8_t     active_uid_[7];   /* Max ISO14443-A UID size */
};

#endif /* TAG_MANAGER_H_ */
