# Usb2BluetoothTouchRelay

Firmware for an M5Stack StampS3 (ESP32-S3) that makes a USB touchscreen work
wirelessly with an Android TV device that has no free USB port. The gateway reads
the panel as a USB HID host and presents it over Bluetooth LE as an absolute
touchscreen, so Android needs no extra app.

```
Touchscreen --USB HID--> StampS3 --BLE HID (HOGP)--> Android TV
```

**Status:** works for single touch end to end: USB touch panel -> StampS3 ->
BLE -> Android, with no app on Android. See [docs/progress.md](docs/progress.md)
for what has actually been verified, and [docs/android-compat.md](docs/android-compat.md)
for limitations (single touch only, some Android TV UI elements ignore touch).
Edge swipes provide Back and Home.

Only one monitor and one Android device have been tested so far
(see [docs/progress.md](docs/progress.md)).

## For users

- Install from the browser (desktop Chrome or Edge):
  `https://gl0wa.github.io/USB2Bluetooth-touch-relay/`
- Read [docs/user-guide.md](docs/user-guide.md): what you need, install,
  connect, pair, navigation, settings, troubleshooting, limitations.
- The user build has no Wi-Fi, OTA or HTTP. Update by re-flashing from the page.

## For developers

OTA, HTTP diagnostics and Wi-Fi exist only in the development build.
The user build (GitHub Pages install) has no networking at all.

| Document | Contents |
| --- | --- |
| [docs/user-guide.md](docs/user-guide.md) | Install, pairing, use, settings, troubleshooting |
| [docs/hardware.md](docs/hardware.md) | StampS3 electrical findings, wiring, safety rules |
| [docs/development.md](docs/development.md) | Toolchain, profiles, flashing, OTA, diagnostics, recovery |
| [docs/android-compat.md](docs/android-compat.md) | How Android TV classifies HID touch devices |
| [docs/progress.md](docs/progress.md) | Verified status and next steps |
| [AGENTS.md](AGENTS.md) | Rules for coding agents working in this repository |

### Navigation gestures

Android TV has no on-screen Back or Home: swipe in from the LEFT edge
for Back, up from the BOTTOM edge for Home. In the development build turn them
off with `python3 tools/u2b.py gestures off` (for example on devices other than
Android TV); in the user build use the page's Settings panel. The setting
survives reboot and re-provisioning.

A D-pad mode covers controls that ignore touch, such as the launcher's
top tabs. Swipe DOWN from the TOP edge to toggle it. In D-pad mode
swipes become arrow keys and a tap becomes Select: swipe for Up/Down/
Left/Right (a long drag repeats), tap for Select. To leave, swipe down
from the top 8% of the screen again, or wait 10 s with no contact. A
slower exit swipe up to 0.8 s is fine; Down swipes that should move
focus must start lower. The LED is cyan
in D-pad mode. `gestures off` disables edge gestures and D-pad mode
together. Edge Back/Home still work while D-pad mode is on.

If touch misbehaves on a device other than Android TV, try standard HID mode
(development: `python3 tools/u2b.py hid-mode standard`; user: Settings panel).

This needs a reboot and a re-pair to apply, and it has no Back/Home keys.
In standard HID mode gestures and D-pad mode stay off and `gestures on`
does not enable them; the saved setting is kept.
Keep `gestures off` as the simple switch for non-Android hosts; use standard
mode only if touch itself misbehaves.

### Connection

Try the simple cable first: monitor touch port to the StampS3 USB-C socket with
a plain USB-C to USB-C cable and the monitor on external power. See
[docs/hardware.md](docs/hardware.md) for what this proves and for the
alternatives.

Read [docs/hardware.md](docs/hardware.md) before connecting anything to the 5V pin.

## Quick start (developers)

```sh
mise install
mise run test                                  # host-side unit tests
mise run build                                 # development build (Wi-Fi, OTA, HTTP)
mise run build-release                         # user build (no networking) + merged USB image
mise run flash -- /dev/cu.usbmodemXXXX         # first flash only, over USB
mise run provision -- /dev/cu.usbmodemXXXX     # Wi-Fi settings and API token (dev only)
mise run ota                                   # every later update, over Wi-Fi (dev only)
python3 tools/u2b.py status
```

## License

MIT. See [LICENSE](LICENSE).
