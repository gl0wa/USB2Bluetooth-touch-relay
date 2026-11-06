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
  host tests (`docs/hardware.md`). Real reports captured on hardware (see above).

- On the real board (ESP32-S3, 8 MB embedded flash): firmware boots, advertises on
  BLE, joins Wi-Fi, and detects a computer and stays in maintenance mode.
- OTA on the real board: a wrong token is rejected, upload takes about 20 s, an
  unconfirmed image rolls back on reboot, and a confirmed image stays. With USB
  host active, an image auto-confirmed under the real rule after about 61 s.

- USB host mode works on the real board: a plain USB-C cable from the monitor's
  touch port runs the StampS3 as host with no computer attached, the panel
  enumerates through its internal hub, and the firmware receives real USB
  reports. The panel sends touches on the absolute-mouse interface (report
  `0x07`) and nothing on the Touch Screen interface, so the gateway is
  single-touch for now. Forwarding real touches to Android is NOT yet verified:
  no BLE reports from real panel input, no events seen by Android, no behaviour
  on screen.

## Not yet run on hardware

The LED, and the touch path beyond receiving USB reports: forwarding real
touches to Android.

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
2. Connection Option A is done; B only if needed. Forward real panel touches to
   Android and check each stage: BLE reports sent, events seen, on-screen behaviour.
3. Full integration, then standalone tests.
