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

## Not yet run on hardware

All firmware: USB host, BLE HID, Wi-Fi, OTA, rollback, mode detection, LED.

## Unknown

- Exact StampS3 revision and flash size.
- VBUS and CC behaviour of the monitor's touch port.
- Whether ESP-IDF hub support works with the monitor's internal hub.
- Which connection option works (`docs/hardware.md`).

## Next

1. Connect the StampS3 to the Mac: identify it, flash, provision, test one OTA cycle.
2. Pair with the Shield and test injected touches (BLE path, no monitor needed).
3. Try connection Option A, then B. Capture real USB reports.
4. Full integration, then standalone tests.
