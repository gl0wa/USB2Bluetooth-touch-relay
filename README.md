# Usb2BluetoothTouchRelay

Firmware for an M5Stack StampS3 (ESP32-S3) that makes a USB touchscreen work
wirelessly with an Android TV device that has no free USB port. The gateway reads
the panel as a USB HID host and presents it over Bluetooth LE as an absolute
touchscreen, so Android needs no extra app.

```
Touchscreen --USB HID--> StampS3 --BLE HID (HOGP)--> Android TV
                            |
                            +-- Wi-Fi: OTA updates, logs, diagnostics
```

**Status:** works for single touch end to end: USB touch panel -> StampS3 ->
BLE -> Android, with no app on Android. See [docs/progress.md](docs/progress.md)
for what has actually been verified, and [docs/android-compat.md](docs/android-compat.md)
for limitations (single touch only, some Android TV UI elements ignore touch,
no on-screen Back/Home).

## What you need

- M5Stack StampS3 (ESP32-S3).
- A USB touchscreen monitor whose touch port presents touches this way.
- A plain USB-C to USB-C cable for the monitor touch port to the StampS3.
- An Android device supporting BLE HID.

Only one monitor and one Android device have been tested so far
(see [docs/progress.md](docs/progress.md)).

## Connection

Try the simple cable first: monitor touch port to the StampS3 USB-C socket with
a plain USB-C to USB-C cable and the monitor on external power. See
[docs/hardware.md](docs/hardware.md) for what this proves and for the
alternatives.

Read [docs/hardware.md](docs/hardware.md) before connecting anything to the 5V pin.

| Document | Contents |
| --- | --- |
| [docs/hardware.md](docs/hardware.md) | StampS3 electrical findings, wiring, safety rules |
| [docs/development.md](docs/development.md) | Toolchain, flashing, OTA, diagnostics, recovery |
| [docs/android-compat.md](docs/android-compat.md) | How Android TV classifies HID touch devices |
| [docs/progress.md](docs/progress.md) | Verified status and next steps |
| [AGENTS.md](AGENTS.md) | Rules for coding agents working in this repository |

## Quick start

```sh
mise install
mise run test                                  # host-side unit tests
mise run build
mise run flash -- /dev/cu.usbmodemXXXX         # first flash only, over USB
mise run provision -- /dev/cu.usbmodemXXXX     # Wi-Fi settings and API token
mise run ota                                   # every later update, over Wi-Fi
python3 tools/u2b.py status
```

## License

MIT. See [LICENSE](LICENSE).
