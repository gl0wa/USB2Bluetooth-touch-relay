# Development

## Toolchain

- ESP-IDF v5.5.2, installed with Espressif's official installer. It is the one
  dependency outside Mise, because it manages its own toolchain and Python
  environment. Default location is `~/esp/esp-idf`; override `IDF_PATH` in
  `.mise.local.toml` if needed.
- Mise tasks source `export.sh` themselves, so there is no shell setup.
- Component versions are declared in `main/idf_component.yml`.

## USB modes

There is one USB PHY, shared by USB Serial/JTAG and USB host. At boot the firmware
waits 1.5 s for USB SOF packets:

| USB-C is connected to | Mode | LED |
| --- | --- | --- |
| A computer | Maintenance: serial console, no USB host | Blue pulse |
| Anything else | USB host, logs over Wi-Fi only | Red blink: no touch device. Amber blink: waiting for Bluetooth. Green: relaying |

Switching between them needs a reset, which replugging provides.

## First setup over USB

```sh
mise run flash -- /dev/cu.usbmodemXXXX
mise run provision -- /dev/cu.usbmodemXXXX
```

Use a 2.4 GHz network, since the ESP32-S3 has no 5 GHz radio. Provisioning writes the
`devcfg` partition and stores a random API token in `.local/device.env`.
Re-provisioning leaves Bluetooth bonds intact.

## Updates and diagnostics over Wi-Fi

```sh
mise run ota                     # build and upload
python3 tools/u2b.py status      # JSON state of USB, BLE and the firmware
python3 tools/u2b.py log -f      # follow the log
python3 tools/u2b.py desc        # USB HID report descriptors, as hex
python3 tools/u2b.py tap 0.5 0.5 # inject a touch into the BLE path
python3 tools/u2b.py swipe 0.2 0.5 0.8 0.5
```

The device answers at `u2b-touch.local`; set `U2B_HOST` if mDNS is unavailable. Every
endpoint needs the token. Traffic is plain HTTP and images are not signed, so
use it only on a trusted network.

A new image must reach Wi-Fi and run for 30 s before it is confirmed. If it crashes
first, or has no Wi-Fi after 180 s, the bootloader returns to the previous image.

## Inspecting the monitor from a Mac

Connect the monitor's touch port to the Mac, then:

```sh
ioreg -p IOUSB -l -w0 | grep -E '"(USB Product Name|idVendor|idProduct)"'
ioreg -r -c IOHIDDevice -l -w0 | grep -E '"(Product|VendorID|ProductID|ReportDescriptor|PrimaryUsage|PrimaryUsagePage)"'
```

Keep serial numbers out of anything committed.

## Recovery

- **Download mode:** with the board unpowered, hold the button while plugging into a
  computer, then flash normally. This is in ROM, so bad firmware cannot disable it.
- **Clear Bluetooth bonds:** hold the button for 5 s while running, or run
  `tools/u2b.py forget`. Also remove the device on the Android side.
- **Lost API token:** run `mise run provision` again after deleting `.local/device.env`.
