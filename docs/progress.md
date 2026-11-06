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
  single-touch for now.

- End to end with real touches: USB touch panel -> StampS3 -> BLE -> SHIELD TV
  (Android 11), with a plain USB-C to USB-C cable from the monitor's touch port
  to the StampS3 and the monitor on external power. No Android app needed.
  On-screen behaviour user-confirmed: taps land in the right place with no
  calibration, one-finger dragging works, opening apps works, Settings can be
  opened, video progress-bar scrubbing and play/pause work.

- Session counters over about 23 minutes: 4632 USB reports, 4631 touches, 4631
  BLE reports sent, 0 failed, 0 USB errors. The image auto-confirmed under the
  OTA rule.

## Unknown

- Exact StampS3 board revision (the chip and flash are confirmed).
- Single touch only: the panel sends touches on its absolute-mouse interface
  (report `0x07`). Multi-touch through the Touch Screen interface is untested.
- Android TV is built for D-pad navigation. Some controls do not respond to
  touch, for example the launcher's top tabs. There is no on-screen Back or
  Home, so touch alone cannot leave an app or Settings; the remote is still
  needed.
- After a reconnect Android takes about 6 s to bring back the input device.
  The Feature report was removed to fix this, but that is only verified through
  uhid: re-pairing has not been done since, so whether the delay is gone over
  BLE is unknown. Android may cache the old map, so forget and re-pair before
  measuring.
- Pairing failed while the Shield was streaming audio to a speaker in another
  room, and worked once it was disconnected. Which factor mattered is unknown.
  Audio together with touch is untested.
- The user confirmed the StampS3 is powered by the monitor alone through the
  USB-C cable, with the monitor on external power. Whether it recovers from a
  cold start and after a monitor power cycle, and whether USB unplug and
  replug recover, is not tested. Long-duration stability is not tested.
- NimBLE logging is still at INFO, left over from debugging.
- OTA and the HTTP API use plain HTTP with a shared token and unsigned images:
  development only.
- One `No route to host` on the first request after boot: seen once, not investigated.
- VBUS and CC behaviour of the monitor's touch port beyond the tested session:
  the user confirmed the monitor powers the StampS3 alone through the USB-C
  cable with the monitor on external power, but VBUS was not measured with a
  meter and cold-start behaviour is untested.

## Next

1. Forget and re-pair, then measure the reconnect delay over BLE.
2. Test pairing and touch with the speaker nearby.
3. Cold-start and USB unplug/replug recovery tests, including after a monitor
   power cycle.
4. Long-run stability test.
5. Set NimBLE logging back to WARNING.
6. Navigation for Back/Home is not working over BLE yet, pending a test of
   the single-characteristic Report ID 0 approach.
7. D-pad mode is planned later, not in this change.
