/**
 * @file ble_peripheral.cpp
 * @brief BLE Peripheral — GATT service and advertising for BLE & NFC Emulator.
 *
 * The BlePeripheral C++ class owns the mutable JSON status payload.
 * Zephyr BLE macros (BT_GATT_SERVICE_DEFINE, BT_CONN_CB_DEFINE) remain
 * at file scope as required by the linker-section mechanism, and delegate
 * to the singleton instance via thin C wrappers.
 */

#include "ble_peripheral.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <errno.h>
#include <string.h>

extern "C" {
#include "zbus_messages.h"
}

LOG_MODULE_REGISTER(ble_peripheral, CONFIG_LOG_DEFAULT_LEVEL);

/* ---- BLE config (single source for this module) ------------------------- */
#define TAG_PERIPHERAL_NAME "ble-nfc-emulator"
#define BLE_SERVICE_UUID    "19B10000-E8F2-537E-4F6C-D104768A1214"

/* BT_UUID_128_ENCODE expects UUID split into 32-16-16-16-48 bit fields. */
#define BLE_SERVICE_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x19B10000, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214)

/* ---- Singleton instance -------------------------------------------------- */
static BlePeripheral ble;
static struct bt_conn *active_conn;
static bool notify_enabled;

/* ---- Custom UUIDs ------------------------------------------------------- */

/* BLE Service UUID: 19B10000-E8F2-537E-4F6C-D104768A1214 */
static struct bt_uuid_128 ble_nfc_emulator_svc_uuid = BT_UUID_INIT_128(
	BLE_SERVICE_UUID_ENCODED);

/* ---- GATT read callbacks (thin C wrappers → singleton) ------------------ */

static ssize_t read_status_json(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 ble.get_status_json(), ble.get_status_json_len());
}

static void status_json_ccc_cfg_changed(const struct bt_gatt_attr *attr,
					uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("BLE JSON notifications %s",
		notify_enabled ? "enabled" : "disabled");
}

/* ---- GATT Service Definition (must be file-scope) ----------------------- */

BT_GATT_SERVICE_DEFINE(ble_nfc_emulator_svc,
	BT_GATT_PRIMARY_SERVICE(&ble_nfc_emulator_svc_uuid),

	/* JSON status characteristic — Read + Notify */
	BT_GATT_CHARACTERISTIC(&status_json_char_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_status_json, NULL, NULL),
	BT_GATT_CCC(status_json_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* ---- Advertising data (must be file-scope) ------------------------------ */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,
		      (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE,
		TAG_PERIPHERAL_NAME, sizeof(TAG_PERIPHERAL_NAME) - 1),
};

/* ---- Connection callbacks (thin C wrappers → singleton) ----------------- */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("BLE connection failed (err 0x%02x)", err);
		return;
	}

	if (active_conn != NULL) {
		bt_conn_unref(active_conn);
	}
	active_conn = bt_conn_ref(conn);
	LOG_INF("BLE connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	if (active_conn != NULL) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}
	notify_enabled = false;

	LOG_INF("BLE disconnected (reason 0x%02x)", reason);
	ble.init(); /* Re-start advertising via the class method */
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* ---- BlePeripheral methods ---------------------------------------------- */

void BlePeripheral::update_status_json(const char *payload, uint8_t payload_len)
{
	if (payload == NULL) {
		return;
	}

	uint8_t max_len = (uint8_t)(sizeof(status_json_) - 1U);
	if (payload_len > max_len) {
		payload_len = max_len;
	}

	memcpy(status_json_, payload, payload_len);
	status_json_[payload_len] = '\0';
	status_json_len_ = payload_len;

	if (active_conn == NULL || !notify_enabled) {
		LOG_DBG("Skip BLE notify: link/subscription not ready");
		return;
	}

	if (!bt_gatt_is_subscribed(active_conn, &ble_nfc_emulator_svc.attrs[2],
				   BT_GATT_CCC_NOTIFY)) {
		LOG_DBG("Skip BLE notify: client not subscribed");
		return;
	}

	/* ATT notification payload must fit within current MTU minus ATT opcode+handle. */
	uint16_t mtu = bt_gatt_get_mtu(active_conn);
	if (mtu <= 3U) {
		LOG_DBG("Skip BLE notify: invalid ATT MTU %u", (unsigned int)mtu);
		return;
	}

	uint16_t max_payload = (uint16_t)(mtu - 3U);
	if (status_json_len_ > max_payload) {
		LOG_DBG("Skip BLE notify: payload %uB exceeds ATT limit %uB",
			(unsigned int)status_json_len_, (unsigned int)max_payload);
		return;
	}

	int ret = bt_gatt_notify(active_conn, &ble_nfc_emulator_svc.attrs[2],
				 status_json_, status_json_len_);
	if (ret < 0) {
		if (ret == -ENOTCONN || ret == -ENOMEM) {
			LOG_DBG("BLE JSON notify deferred: %d", ret);
		} else {
			LOG_WRN("BLE JSON notify failed: %d", ret);
		}
	} else {
		LOG_INF("BLE notify JSON (%uB): %s",
			(unsigned int)status_json_len_, status_json_);
	}
}

int BlePeripheral::start_advertising()
{
	/* BT_LE_ADV_CONN uses a C compound literal which is invalid in C++.
	 * Define the equivalent struct manually. */
	static const struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0,
		.secondary_max_skip = 0,
		.options = BT_LE_ADV_OPT_CONN,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
		.peer = NULL,
	};

	int err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err && err != -EALREADY) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return err;
	}
	advertising_ = true;
	LOG_INF("BLE advertising started as \"%s\"", TAG_PERIPHERAL_NAME);
	return 0;
}

int BlePeripheral::init()
{
	if (!advertising_) {
		int err = bt_enable(NULL);
		if (err) {
			LOG_ERR("Bluetooth init failed (err %d)", err);
			return err;
		}
		LOG_INF("Bluetooth initialized");
		LOG_INF("BLE cfg | name=%s | svc=%s",
			TAG_PERIPHERAL_NAME, BLE_SERVICE_UUID);
	}

	return start_advertising();
}

/* ---- Zbus subscriber (C linkage) ---------------------------------------- */
extern "C" {
ZBUS_SUBSCRIBER_DEFINE(ble_peripheral_sub, 4);
ZBUS_CHAN_ADD_OBS(smarttag_status, ble_peripheral_sub, 3);
}

/* ---- BLE Notification Thread -------------------------------------------- */
static void ble_peripheral_thread(void *, void *, void *)
{
	const struct zbus_channel *chan;

	LOG_INF("BLE Peripheral thread started — waiting for smarttag_status");

	while (true) {
		if (zbus_sub_wait(&ble_peripheral_sub, &chan, K_FOREVER) == 0) {
			struct smarttag_status_msg status;

			if (zbus_chan_read(&smarttag_status, &status,
					  K_MSEC(100)) == 0) {
				uint8_t payload_len =
					(uint8_t)strnlen(status.payload,
							 sizeof(status.payload));
				ble.update_status_json(status.payload, payload_len);
			}
		}
	}
}

K_THREAD_DEFINE(ble_peripheral_tid, 2048,
		ble_peripheral_thread, NULL, NULL, NULL,
		8, 0, 0);

/* ---- Initialization ----------------------------------------------------- */
static int ble_peripheral_sys_init(void)
{
	return ble.init();
}

SYS_INIT(ble_peripheral_sys_init, APPLICATION, 99);
