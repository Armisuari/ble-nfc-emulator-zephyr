/**
 * @file ble_peripheral.h
 * @brief BLE Peripheral — custom GATT service for BLE & NFC Emulator.
 *
 * Defines the BLE & NFC Emulator Service with one JSON status characteristic.
 *
 * The Zephyr BLE macros (BT_GATT_SERVICE_DEFINE, BT_CONN_CB_DEFINE) must
 * remain at file scope in the .cpp file.  This class encapsulates the
 * mutable BLE state and the methods that operate on it.
 */

#ifndef BLE_PERIPHERAL_H_
#define BLE_PERIPHERAL_H_

#include <cstdint>
#include <string.h>

extern "C" {
#include "zbus_messages.h"
}

/**
 * @class BlePeripheral
 * @brief Manages BLE state and pushes GATT notifications.
 */
class BlePeripheral {
public:
	BlePeripheral()
		: status_json_len_(0)
		, advertising_(false)
	{
		const char *default_payload = "{\"bleNfcEmulatorId\":\"\"}";
		(void)strncpy(status_json_, default_payload, sizeof(status_json_) - 1U);
		status_json_[sizeof(status_json_) - 1U] = '\0';
		status_json_len_ = (uint8_t)strlen(status_json_);
	}

	/**
	 * Update the cached status JSON and push a BLE notification.
	 * @param payload JSON payload bytes.
	 * @param payload_len payload length in bytes.
	 */
	void update_status_json(const char *payload, uint8_t payload_len);

	/**
	 * Initialize the BLE subsystem and start advertising.
	 * @return 0 on success, negative errno on failure.
	 */
	int init();

	/* Accessors used by the C-linkage GATT read callbacks */
	const char *get_status_json() const { return status_json_; }
	uint8_t get_status_json_len() const { return status_json_len_; }

private:
	char     status_json_[SMARTTAG_JSON_PAYLOAD_MAX_LEN];
	uint8_t  status_json_len_;
	bool     advertising_;

	/** Restart advertising after a disconnect. */
	int start_advertising();
};

#endif /* BLE_PERIPHERAL_H_ */
