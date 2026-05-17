/**
 * @file ble_peripheral.cpp
 * @brief BLE Peripheral — GATT service and advertising for BLE & NFC Emulator.
 *
 * BlePeripheral owns the JSON status payload. Zephyr BLE macros
 * (BT_GATT_SERVICE_DEFINE, BT_CONN_CB_DEFINE) must stay at file scope so the
 * linker can place them into their iterable sections; they delegate to the
 * singleton via thin C wrappers.
 */

#include "ble_peripheral.h"

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

extern "C" {
#include "zbus_messages.h"
}

LOG_MODULE_REGISTER(ble_peripheral, CONFIG_LOG_DEFAULT_LEVEL);

#define BLE_PERIPHERAL_NAME "ble-nfc-emulator"

/* Service UUID: 19B10000-E8F2-537E-4F6C-D104768A1214
 * Status JSON characteristic UUID: 19B10001-E8F2-537E-4F6C-D104768A1214 */
#define BLE_NFC_EMULATOR_SVC_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x19B10000, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214)
#define BLE_NFC_EMULATOR_STATUS_CHR_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x19B10001, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214)

static struct bt_uuid_128 svc_uuid =
	BT_UUID_INIT_128(BLE_NFC_EMULATOR_SVC_UUID_ENCODED);
static struct bt_uuid_128 status_json_char_uuid =
	BT_UUID_INIT_128(BLE_NFC_EMULATOR_STATUS_CHR_UUID_ENCODED);

/* ---- Module state ------------------------------------------------------- */
static BlePeripheral ble;
static struct bt_conn *active_conn;
static bool notify_enabled;

/* ---- GATT callbacks ----------------------------------------------------- */

static ssize_t read_status_json(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 ble.get_status_json(),
				 ble.get_status_json_len());
}

static void status_json_ccc_cfg_changed(const struct bt_gatt_attr *attr,
					uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("BLE JSON notifications %s",
		notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(ble_nfc_emulator_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&status_json_char_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_status_json, NULL, NULL),
	BT_GATT_CCC(status_json_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* attrs[1] is the characteristic value handle (the one notifications target). */
#define STATUS_VALUE_ATTR (&ble_nfc_emulator_svc.attrs[1])

/* ---- Advertising payload ----------------------------------------------- */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,
		      (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE,
		BLE_PERIPHERAL_NAME, sizeof(BLE_PERIPHERAL_NAME) - 1),
};

/* ---- Connection callbacks ---------------------------------------------- */

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
	(void)ble.restart_advertising();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* ---- BlePeripheral implementation -------------------------------------- */

BlePeripheral::BlePeripheral()
	: status_json_len_(0),
	  bt_enabled_(false)
{
	static const char default_payload[] = "{\"state\":\"idle\",\"uid\":\"--\"}";
	memcpy(status_json_, default_payload, sizeof(default_payload));
	status_json_len_ = (uint8_t)(sizeof(default_payload) - 1U);
}

int BlePeripheral::start_advertising()
{
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
		LOG_ERR("Advertising start failed (err %d)", err);
		return err;
	}
	LOG_INF("BLE advertising as \"%s\"", BLE_PERIPHERAL_NAME);
	return 0;
}

int BlePeripheral::init()
{
	if (!bt_enabled_) {
		int err = bt_enable(NULL);
		if (err) {
			LOG_ERR("Bluetooth init failed (err %d)", err);
			return err;
		}
		bt_enabled_ = true;
		LOG_INF("Bluetooth initialized");
	}
	return start_advertising();
}

int BlePeripheral::restart_advertising()
{
	return start_advertising();
}

void BlePeripheral::update_status_json(const char *payload, uint8_t payload_len)
{
	if (payload == NULL || payload_len == 0U) {
		return;
	}

	const uint8_t max_len = (uint8_t)(sizeof(status_json_) - 1U);
	if (payload_len > max_len) {
		payload_len = max_len;
	}

	memcpy(status_json_, payload, payload_len);
	status_json_[payload_len] = '\0';
	status_json_len_ = payload_len;

	if (active_conn == NULL || !notify_enabled) {
		LOG_DBG("Skip notify: link/subscription not ready");
		return;
	}

	const uint16_t mtu = bt_gatt_get_mtu(active_conn);
	if (mtu <= 3U) {
		LOG_DBG("Skip notify: invalid ATT MTU %u", mtu);
		return;
	}

	const uint16_t max_payload = (uint16_t)(mtu - 3U);
	if (status_json_len_ > max_payload) {
		LOG_DBG("Skip notify: payload %uB exceeds ATT limit %uB",
			status_json_len_, max_payload);
		return;
	}

	int ret = bt_gatt_notify(active_conn, STATUS_VALUE_ATTR,
				 status_json_, status_json_len_);
	if (ret < 0) {
		if (ret == -ENOTCONN || ret == -ENOMEM) {
			LOG_DBG("BLE notify deferred: %d", ret);
		} else {
			LOG_WRN("BLE notify failed: %d", ret);
		}
	} else {
		LOG_INF("BLE notify (%uB): %s", status_json_len_, status_json_);
	}
}

/* ---- Zbus subscriber thread -------------------------------------------- */

extern "C" {
ZBUS_SUBSCRIBER_DEFINE(ble_peripheral_sub, 4);
ZBUS_CHAN_ADD_OBS(ble_nfc_emulator_status, ble_peripheral_sub, 3);
}

static void ble_peripheral_thread(void *, void *, void *)
{
	const struct zbus_channel *chan;
	struct ble_nfc_emulator_status_msg status;

	LOG_INF("BLE Peripheral thread started — waiting for ble_nfc_emulator_status");

	while (true) {
		if (zbus_sub_wait(&ble_peripheral_sub, &chan, K_FOREVER) != 0) {
			continue;
		}
		if (zbus_chan_read(&ble_nfc_emulator_status, &status,
				   K_MSEC(100)) != 0) {
			continue;
		}
		const uint8_t len = (uint8_t)strnlen(status.payload,
						     sizeof(status.payload));
		ble.update_status_json(status.payload, len);
	}
}

K_THREAD_DEFINE(ble_peripheral_tid, 2048,
		ble_peripheral_thread, NULL, NULL, NULL,
		8, 0, 0);

static int ble_peripheral_sys_init(void)
{
	return ble.init();
}

SYS_INIT(ble_peripheral_sys_init, APPLICATION, 99);
