/**
 * @file ble_peripheral.h
 * @brief BLE Peripheral — custom GATT service for BLE & NFC Emulator.
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
	BlePeripheral();

	/** Initialize the BLE stack and start advertising (idempotent). */
	int init();

	/** Update the cached status JSON and push a BLE notification if possible. */
	void update_status_json(const char *payload, uint8_t payload_len);

	/** Restart advertising (called after disconnect). */
	int restart_advertising();

	/* Accessors used by the C-linkage GATT read callbacks. */
	const char *get_status_json() const { return status_json_; }
	uint8_t get_status_json_len() const { return status_json_len_; }

private:
	int start_advertising();

	char    status_json_[BLE_NFC_EMULATOR_JSON_PAYLOAD_MAX_LEN];
	uint8_t status_json_len_;
	bool    bt_enabled_;
};

#endif /* BLE_PERIPHERAL_H_ */
