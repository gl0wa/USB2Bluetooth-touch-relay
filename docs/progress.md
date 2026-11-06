# Progress

## Verified

- ESP-IDF v5.5.2 builds the firmware for ESP32-S3.
- Host unit tests pass under ASan and UBSan, including fuzzed descriptors.
- StampS3 v0.2 schematic reviewed (`docs/hardware.md`).
- ADB access to the Shield works.
- Through uhid, the Shield treats a Contact ID digitizer as a true touchscreen
  (`docs/android-compat.md`). A variant of that descriptor without the Feature
  report also appears through uhid in under 1 s as `touchScreen` in direct mode.
- Over real BLE the Shield pairs with the gateway, classifies it as `touchScreen`
  in direct mode (source `0x1002`), and an injected tap and swipe arrive exactly
  (34 reports sent, 34 `SYN_REPORT`, 2 presses, 2 releases). After a gateway
  reboot it reconnects by itself in about 3 s with the bond kept, and touch
  still works (`docs/android-compat.md`).

- Independent pre-flash code review completed; verified defects fixed. Plain HTTP
  and unsigned OTA are accepted for development; revisit before any production use.

- Monitor USB topology and descriptors captured; the parser decodes that layout in
  host tests (`docs/hardware.md`). No real reports captured yet.

- On the real board (ESP32-S3, 8 MB embedded flash): firmware boots, advertises on
  BLE, joins Wi-Fi, and detects a computer and stays in maintenance mode.
- OTA on the real board: a wrong token is rejected, upload takes about 20 s, an
  unconfirmed image rolls back on reboot, and a confirmed image stays.

## Not yet run on hardware

USB host, hub enumeration, the LED, and the touch path.

## Unknown

- Exact StampS3 board revision (the chip and flash are confirmed).
- VBUS and CC behaviour of the monitor's touch port.
- Whether ESP-IDF hub support works with the monitor's internal hub.
- One `No route to host` on the first request after boot: seen once, not investigated.
- Whether the A2DP stream or the distance blocked the first pairing attempts.
- Whether the no-Feature report map removes the ~6 s delay over BLE (it is
  verified through uhid only). Android may cache the old map, so retest after
  forget and re-pair.

## Next

1. Verify the no-Feature report map over BLE (forget and re-pair first).
2. Try connection Option A, then B. Capture real USB reports.
3. Full integration, then standalone tests.
