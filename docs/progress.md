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

- Back and Home work on screen over BLE through edge swipes and the StampS3 button,
  confirmed by the user: 46 key reports sent, 0 failed, alongside 1723 touch reports.
- After a reconnect the input device returns in about 1.2 s, down from 6-7 s.
- D-pad mode confirmed by the user on screen over BLE: entering by top swipe, arrow
  swipes, the exit swipe, Select, the idle exit and the cyan LED. Back and Home keep
  working inside it.

## Unknown

- Exact StampS3 board revision (the chip and flash are confirmed).
- Single touch only: the panel sends touches on its absolute-mouse interface
  (report `0x07`). Multi-touch through the Touch Screen interface is untested.
- Android TV is built for D-pad navigation. Some controls do not respond to
  touch, for example the launcher's top tabs. Back and Home come from edge swipes;
  a D-pad mode (top-edge toggle, swipes to arrows, tap to Select) exists in
  firmware and works on screen over BLE.
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

## Untested (new in this change, not run on hardware)

- User build (`CONFIG_U2B_NETWORK=n`, `mise run build-release`): both images build
  locally and the user ELF has no `httpd_*`/`mdns_*`/`netsvc_*`/`esp_wifi_*` symbols
  (only BT coexistence tables mention "wifi"), but it has not been flashed or run.
- Serial settings (`status`, `gestures on/off`, `hid android/standard`, `forget`,
  `reboot` with `U2B ` JSON lines): compiles in both builds, not exercised over USB.
- Web install page (`web/`) and GitHub Pages flashing: not tried on hardware; needs
  desktop Chrome or Edge.
- Release workflow (tag → Pages + GitHub release): YAML parses and the local build
  and merge commands pass, but the workflow itself has not run.

## Next

1. Test pairing and touch with the speaker nearby.
2. Cold-start and USB unplug/replug recovery tests, including after a monitor
   power cycle.
3. Long-run stability test.
