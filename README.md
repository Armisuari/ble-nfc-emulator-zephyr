# BLE & NFC Emulator (nRF54L15 DK)

Zephyr firmware that reads an ISO 14443-A NFC tag through a PN532 over I²C
and republishes a compact JSON status string over a custom BLE GATT service.

It is laid out as a small Zbus-based pipeline so each module can be built,
swapped, or skipped without touching the others.

---

## Pipeline at a glance

- **`nfc_reader_pn532`** — drives the PN532 over I²C, debounces detect/remove,
  publishes `nfc_event_msg` onto the `nfc_events` Zbus channel.
- **`tag_manager`** — subscribes to `nfc_events`, runs an idle → reading →
  verified/error state machine, publishes JSON status onto
  `ble_nfc_emulator_status`.
- **`ble_peripheral`** — subscribes to `ble_nfc_emulator_status` and pushes
  the payload out as a BLE GATT notification on a custom 128-bit service.
- **`nfc_event_probe`** *(optional)* — debug subscriber that logs every
  `nfc_events` packet.
- **`i2c_scanner`** *(optional)* — one-shot 7-bit address scan at boot.

---

## Repository layout

```
.
├── CMakeLists.txt              # Conditional source list driven by Kconfig
├── kconfig                     # Application Kconfig options
├── prj.conf                    # Full app profile (BLE + Zbus)
├── prj-pn532.conf              # PN532 bring-up profile (I²C + RTT, no BLE)
├── boards/
│   └── nrf54l15dk_nrf54l15_cpuapp_ns.overlay   # PN532 I²C wiring
└── src/
    ├── main.cpp                # Boot banner only
    ├── zbus_channels.c         # ZBUS_CHAN_DEFINE (C only)
    ├── zbus_messages.h         # Shared message types + channel declarations
    └── modules/
        ├── app/
        │   ├── tag_manager.{h,cpp}     # NFC state machine
        │   ├── nfc_event_probe.cpp     # Optional debug observer
        │   ├── nfc_uid_utils.h         # UID formatting helper
        │   ├── i2c_scanner.{h,cpp}     # Bring-up I²C scan
        │   └── ...
        ├── drivers/
        │   └── pn532/nfc_reader_pn532.{h,cpp}  # PN532 over I²C
        └── middleware/
            └── ble_peripheral.{h,cpp}          # Custom GATT service
```

---

## Build profiles

| File | Purpose | BLE | PN532 | RTT |
| --- | --- | --- | --- | --- |
| `prj.conf` | Full app (BLE + Zbus + PN532) | ✅ | ✅ | ✅ |
| `prj-pn532.conf` | PN532-only bring-up (no BLE) | ✗ | ✅ | ✅ |

The two profiles are independent — pick the one you want with
`-DCONF_FILE=...`.

### Build commands (west)

PN532 bring-up:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p auto . -- -DCONF_FILE=prj-pn532.conf
west flash --runner jlink
```

Full app:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p auto . -- -DCONF_FILE=prj.conf
west flash --runner jlink
```

`CMakeLists.txt` registers this directory as a `BOARD_ROOT`, so the local
`boards/` overlay is picked up automatically.

### Windows / PowerShell notes

Two gotchas when building this tree on Windows:

1. **Quote `-DCONF_FILE=...`** — PowerShell splits the argument on the `.`
   in `.conf`, so `west` ends up looking for a file named `prj-pn532`. Wrap
   it in double quotes:

   ```powershell
   west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p auto . -- "-DCONF_FILE=prj-pn532.conf"
   ```

2. **Use a short build directory** — the TF-M build for `cpuapp/ns` generates
   very deep object paths (`secure_fw/.../__/__/__/generated/...`). Combined
   with the user-profile-rooted source path, the resulting `.o.d` filenames
   exceed Windows' 260-character MAX_PATH and gcc fails with
   `No such file or directory` on a perfectly valid path. Point `west` at a
   short build root with `-d`:

   ```powershell
   west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p auto -d C:\b\nfc . -- "-DCONF_FILE=prj-pn532.conf"
   ```

   Alternative: enable Windows long-path support system-wide
   (`HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled = 1`,
   reboot required). The short-build-dir approach is less invasive.

---

## Kconfig options (under `BLE & NFC Emulator Application`)

| Symbol | Default | Notes |
| --- | --- | --- |
| `ENV_NFC_PN532` | `y` | Selects PN532 as the NFC source. |
| `NFC_PN532` | tracks `ENV_NFC_PN532` | Internal — compiles the PN532 driver. |
| `INCLUDE_APP_PIPELINE` | `y` | Compiles `tag_manager` + `ble_peripheral` (the latter only when `CONFIG_BT=y`). Disable for driver-only/bring-up builds. |
| `PN532_POLL_PERIOD_MS` | `120` | Poll cadence for `InListPassiveTarget`. |
| `PN532_PUBLISH_TIMEOUT_MS` | `100` | Timeout for `zbus_chan_pub` on `nfc_events`. |
| `PN532_DETECT_STREAK` | `2` | Consecutive UID hits before publishing *detected*. |
| `PN532_REMOVE_STREAK` | `3` | Consecutive misses before publishing *removed*. |
| `NFC_TEST_OBSERVER` | `n` | Adds the debug Zbus subscriber that logs nfc_events. |
| `STRICT_UID_VERIFY` | `n` | If `y`, tag_manager only accepts `TagManager::TARGET_UID`. |
| `I2C_SCANNER_ON_BOOT` | `n` | One-shot I²C scan during `nfc_reader_pn532::init()`. |

---

## Zbus channels & messages

Defined in [`src/zbus_messages.h`](src/zbus_messages.h) and
[`src/zbus_channels.c`](src/zbus_channels.c).

```c
struct nfc_event_msg {
    uint8_t tag_id[7];
    uint8_t tag_id_len;
    bool    detected;
    int64_t timestamp_us;
};

struct ble_nfc_emulator_status_msg {
    char payload[BLE_NFC_EMULATOR_JSON_PAYLOAD_MAX_LEN]; /* 80 */
};
```

| Channel | Producer | Consumer(s) |
| --- | --- | --- |
| `nfc_events` | `nfc_reader_pn532` | `tag_manager`, `nfc_event_probe` |
| `ble_nfc_emulator_status` | `tag_manager` | `ble_peripheral` |

The status payload is a small JSON object, e.g.

```json
{"state":"verified","uid":"04:BA:DF:00"}
```

---

## BLE GATT layout

`ble_peripheral` advertises as **`ble-nfc-emulator`** and exposes one
primary service with one Read/Notify characteristic.

| | UUID |
| --- | --- |
| Service | `19B10000-E8F2-537E-4F6C-D104768A1214` |
| Status JSON characteristic | `19B10001-E8F2-537E-4F6C-D104768A1214` |

Subscribe to the characteristic (CCCD → notify) from any BLE client
(e.g. `nRF Connect`) and you will receive a notification every time
`tag_manager` reports a state change.

---

## Hardware wiring (PN532 ↔ nRF54L15 DK)

Defaults in
[`boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay`](boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay):

| PN532 pin | nRF54L15 DK pin | Notes |
| --- | --- | --- |
| SDA | P1.07 | TWIM SDA, internal pull-up via `bias-pull-up` |
| SCL | P1.06 | TWIM SCL, internal pull-up via `bias-pull-up` |
| VCC | 3V3 | |
| GND | GND | |
| I²C address | `0x24` | PN532 in I²C mode |

If your wiring differs, edit the `psels` lines and `reg = <0x24>;` in the
overlay.

---

## Bring-up checklist

1. Flash `prj-pn532.conf` and watch RTT (`JLinkRTTViewer` or
   `west espressif monitor`-style tools).
2. The I²C scanner should log `I2C device found at 0x24`.
3. PN532 firmware-version + SAM-config exchanges should succeed before
   the polling thread starts.
4. Touch a Mifare Classic / Ultralight / NTAG to the antenna — expect
   `PN532 tag detected (uid_len=…)` followed by `Tag DETECTED [..:..]`
   from `tag_manager`.
5. Pull the tag away → `PN532 tag removed` → `Tag REMOVED -> IDLE`.
6. Switch to `prj.conf`, connect over BLE, subscribe to the status
   characteristic, and verify notifications match the FSM transitions.

---

## What changed in the latest pass

- `prj-pn532.conf` now enables `CONFIG_ZBUS`, `CONFIG_ZBUS_RUNTIME_OBSERVERS`,
  `CONFIG_CPP` and `CONFIG_STD_CPP11`. The PN532-only profile shares the same
  C++ sources and zbus-based publish/subscribe flow as the full app, so
  dropping those configs caused undefined references to `zbus_chan_pub` /
  `zbus_sub_wait` / `zbus_chan_read` and a `STATIC_INIT_GNU not enabled`
  linker error.
- Documented the two Windows/PowerShell build gotchas (quoting
  `-DCONF_FILE=...` and using a short `-d` build root to dodge MAX_PATH on
  the TF-M build).
- Reconciled all `SMARTTAG_*` leftover identifiers with the
  `BLE_NFC_EMULATOR_*` / unprefixed Kconfig naming actually in use.
- Repaired the broken `ZBUS_CHAN_DECLARE` in `zbus_messages.h` (it was
  silently declaring three bogus channels).
- Added the missing `INCLUDE_APP_PIPELINE` Kconfig that
  `CMakeLists.txt` relies on, and dropped dead references to files that
  do not exist (`data_aggregator.cpp`, `ui_manager.cpp`,
  `board_init.cpp`).
- Defined the missing status-JSON characteristic UUID, fixed the GATT
  attribute index used by `bt_gatt_notify`, and made BLE
  re-advertisement on disconnect call `bt_enable()` exactly once
  (instead of every reconnect attempt).
- `tag_manager` now actually publishes JSON onto
  `ble_nfc_emulator_status` so the BLE peripheral has data to notify.
- Minor: removed the `112` magic number in the I²C scanner, tightened
  the PN532 `last_uid_` update so stale bytes are cleared on each
  fresh UID.
