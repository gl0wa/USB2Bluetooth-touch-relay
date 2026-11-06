# Progress

## Verified

- ESP-IDF v5.5.2 builds the firmware for ESP32-S3.
- Host unit tests pass under ASan and UBSan, including fuzzed descriptors.
- StampS3 v0.2 schematic reviewed (`docs/hardware.md`).
- ADB access to the Shield works.
- Through uhid, the Shield treats a Contact ID digitizer as a true touchscreen
  (`docs/android-compat.md`).

- Independent pre-flash code review completed; verified defects fixed. Plain HTTP
  and unsigned OTA are accepted for development; revisit before any production use.

- Monitor USB topology and descriptors captured; the parser decodes that layout in
  host tests (`docs/hardware.md`). No real reports captured yet.

- On the real board (ESP32-S3, 8 MB embedded flash): firmware boots, advertises on
  BLE, joins Wi-Fi, and detects a computer and stays in maintenance mode.
- OTA on the real board: a wrong token is rejected, upload takes about 20 s, an
  unconfirmed image rolls back on reboot, and a confirmed image stays.

## Not yet run on hardware

BLE pairing and HID reports, USB host, hub enumeration, the LED, and the touch path.

## Unknown

- Exact StampS3 board revision (the chip and flash are confirmed).
- VBUS and CC behaviour of the monitor's touch port.
- Whether ESP-IDF hub support works with the monitor's internal hub.
- One `No route to host` on the first request after boot: seen once, not investigated.

## Next

1. Pair with the Shield and test injected touches (BLE path, no monitor needed).
2. Try connection Option A, then B. Capture real USB reports.
3. Full integration, then standalone tests.
