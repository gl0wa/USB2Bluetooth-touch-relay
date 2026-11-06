# Development

## Toolchain

- ESP-IDF v5.5.2, installed with Espressif's official installer. It is the one
  dependency outside Mise, because it manages its own toolchain and Python
  environment. Default location is `~/esp/esp-idf`; override `IDF_PATH` in
  `.mise.local.toml` if needed.
- Mise tasks source `export.sh` themselves, so there is no shell setup.
- Component versions are declared in `main/idf_component.yml`.

## Build profiles

Two firmware profiles. The report map, gesture thresholds and partition table
are identical in both; only networking differs.

| Profile | Kconfig | Networking | Use |
| --- | --- | --- | --- |
| Development (default) | `CONFIG_U2B_NETWORK=y` | Wi-Fi, OTA, HTTP diagnostics (`main/netsvc.c`) | Daily work over Wi-Fi |
| User | `CONFIG_U2B_NETWORK=n` | None; USB Serial/JTAG settings only | GitHub Pages install |

- `main/Kconfig.projbuild` defines `U2B_NETWORK` (default y).
- `sdkconfig.release` sets `CONFIG_U2B_NETWORK=n` plus user-build logging
  (`WARN`). It is applied after `sdkconfig.defaults` via
  `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release"`, so
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` and the partition table stay the same.
- `main/CMakeLists.txt` compiles `netsvc.c` and its `REQUIRES`
  (`esp_wifi`, `esp_netif`, `esp_event`, `esp_http_server`) only when
  `CONFIG_U2B_NETWORK` is set. The user image must not link Wi-Fi, HTTP server
  or mDNS (checked with `nm`/`strings`; only BT coexistence tables mention
  "wifi", no `esp_wifi_*`/`httpd_*`/`mdns_*`/`netsvc_*` symbols).
- `main/app_main.c` guards `netsvc_start`, the rollback timer and related state
  with `CONFIG_U2B_NETWORK`. Without networking there is no timed rollback and
  no reference to `netsvc`.
- Dev-build OTA and rollback behaviour is unchanged.

```sh
mise run build            # development, into build/
mise run build-release    # user build into build-release/, then merged USB image
```

`build-release` uses a separate sdkconfig under that directory and produces
`build-release/u2b-touch-merged.bin` via `tools/merge_firmware.py`
(`esptool.py merge_bin`, starting at offset 0: bootloader, partition table,
`ota_data_initial`, app at the offsets from `build-release/flasher_args.json`,
never hardcoded). `ota_data_initial` is included so a device that previously ran
from `ota_1` boots the new image. `build-release/` is git-ignored.
`mise run build` is unchanged.

Sizes from local builds (6 Nov 2026, ESP-IDF v5.5.2):
development app `1,066,368` bytes, user app `582,176` bytes,
merged USB image `713,248` bytes.

## USB modes

There is one USB PHY, shared by USB Serial/JTAG and USB host. At boot the firmware
waits 1.5 s for USB SOF packets:

| USB-C is connected to | Mode | LED |
| --- | --- | --- |
| A computer | Maintenance: serial console, no USB host | Blue pulse |
| Anything else | USB host, logs over Wi-Fi only | Red blink: no touch device. Yellow blink: waiting for Bluetooth. Green: relaying. Cyan: D-pad mode |

Switching between them needs a reset, which replugging provides.

Serial settings (`main/serial_cfg.c`, both builds) run only in maintenance mode:
USB host mode has no serial, so the task starts only when `maintenance == true`.
It uses the `usb_serial_jtag` driver from a small task to read lines. The startup
console already uses USB Serial/JTAG in direct-FIFO ("no driver") mode, so the task
installs the driver (if needed) and switches the VFS to it with
`usb_serial_jtag_vfs_use_driver()`: both ESP_LOG output and the responses then share
the same TX ring, so logging keeps working while this task owns reading. The task is
the only reader (the app never reads stdin), polls with a short timeout so nothing
blocks, uses a bounded line buffer, and writes each response in a single
`usb_serial_jtag_write_bytes` call as one `U2B {...}` line.

Commands, one per line: `status`, `gestures on`, `gestures off`, `hid android`,
`hid standard`, `forget`, `reboot`. Every response is one line starting with `U2B `
then JSON, so the web page can filter it from log output. Example:

`U2B {"ok":true,"version":"...","build":"user","gestures":1,"gestures_saved":1,"standard_hid":0,"standard_hid_saved":0,"bonds":1,"reboot_required":false}`

Errors: `U2B {"ok":false,"error":"..."}`. `build` is `"user"` or `"dev"`. Both
effective and saved values are reported (standard mode forces gestures off;
`hid` applies after reboot). `hid` changes report `reboot_required:true` when the
saved mode differs from the running one. The task reuses `relay_set_gestures`,
`relay_save_standard_hid` and `ble_hogp_clear_bonds`; saving lives in `app_main.c`
so it exists without networking. Never prints credentials; no command reads `devcfg`.

## First setup over USB

```sh
mise run flash -- /dev/cu.usbmodemXXXX
mise run provision -- /dev/cu.usbmodemXXXX
```

Use a 2.4 GHz network, since the ESP32-S3 has no 5 GHz radio. Provisioning writes the
`devcfg` partition and stores a random API token in `.local/device.env`.
Re-provisioning leaves Bluetooth bonds intact.

## Updates and diagnostics over Wi-Fi (development build only)

Wi-Fi, OTA and the HTTP API exist only in the development build. The user build
has no networking; update it by re-flashing from the install page.

```sh
mise run ota                     # build and upload
python3 tools/u2b.py status      # JSON state of USB, BLE and the firmware
python3 tools/u2b.py log -f      # follow the log
python3 tools/u2b.py desc        # USB HID report descriptors, as hex
python3 tools/u2b.py tap 0.5 0.5 # inject a touch into the BLE path
python3 tools/u2b.py swipe 0.2 0.5 0.8 0.5
python3 tools/u2b.py wifi-pause 90 # test Bluetooth without Wi-Fi radio sharing
```

`swipe` sends a single request; the gateway interpolates the drag on-device,
submitting a point about every 15 ms.

## Navigation gestures

Android TV has no on-screen Back or Home. Swipe in from the LEFT edge,
moving right, for Back; swipe up from the BOTTOM edge for Home. A short
press of the StampS3 button is Back as a fallback. Edge taps and drags
still work as touches, only slightly late.

A D-pad mode covers controls that ignore touch, such as the launcher's
top tabs. Swipe DOWN from the TOP edge to toggle it. In D-pad mode
swipes become arrow keys and a tap becomes Select: swipe right/left/up
(y decreasing)/down for the arrows, a long drag repeats, tap for Select.
To leave, swipe down from the top 8% of the screen again, or wait 10 s
with no contact (auto exit). A slower exit swipe up to 0.8 s is fine;
Down swipes that should move focus must start lower. The LED is cyan in
D-pad mode, taking priority over green.
`gestures off` disables edge gestures and D-pad mode together. Edge
Back/Home still work while D-pad mode is on; only the top toggle leaves it.

```sh
python3 tools/u2b.py gestures off  # or: on
python3 tools/u2b.py key back       # test Bluetooth keys without gestures
```

The switch lives in default NVS (`cfg` namespace), so it survives reboot
and re-provisioning (which only rewrites `devcfg`). The BLE report map
stays identical when gestures are off, so toggling never needs re-pairing.
Turn gestures off for devices other than Android TV, where edge swipes
should stay touches.

For touch problems on devices other than Android TV, try standard HID mode:

```sh
python3 tools/u2b.py hid-mode standard  # or: android
```

This saves `standard_hid` in default NVS (`cfg` namespace), then needs a
reboot and a re-pair to apply. Standard mode uses normal HOGP report IDs
and has no keys: gestures and D-pad mode stay off while it is active, and
`gestures on` does not enable them (the saved setting is kept). Android TV
uses the default `android` mode.

The device answers at `u2b-touch.local`; set `U2B_HOST` if mDNS is unavailable. Every
endpoint needs the token.

**Security limitation:** traffic is plain HTTP and images are not signed. Anyone
who can capture traffic on the network can read the token and reuse it to upload
firmware or inject touches. Use it only on a trusted network.

A new image is confirmed after 30 s of stable Wi-Fi followed by 30 s with a touch
device attached in USB host mode. Until then any reset returns to the previous
image, and without stable Wi-Fi for 180 s it rolls back by itself. With no touch
device it keeps running unconfirmed: use `tools/u2b.py confirm` to accept it, or
upload another image, which accepts the running one first because OTA cannot start
otherwise. The previous image is then overwritten, so a bad upload falls back only
to the image that received it.

## Release process

- Tag `v*` (full git history, so the firmware version matches the tag).
- `.github/workflows/release.yml`: builds the user image in the
  `espressif/idf:v5.5.2` container, generates `manifest.json` from
  `web/manifest.template.json` (`__VERSION__` → tag, `__MERGED_BIN__` →
  `u2b-touch-merged.bin`), assembles `web/` plus firmware into a Pages artifact,
  deploys with `actions/configure-pages`, `actions/upload-pages-artifact` and
  `actions/deploy-pages`, and attaches the merged binary to a GitHub release.
  Least-privilege `permissions`; `workflow_dispatch` redeploys.
- The merge logic lives only in `tools/merge_firmware.py`, used by
  `mise run build-release` and CI.

## Inspecting the monitor from a Mac

Connect the monitor's touch port to the Mac, then:

```sh
ioreg -p IOUSB -l -w0 | grep -E '"(USB Product Name|idVendor|idProduct)"'
ioreg -r -c IOHIDDevice -l -w0 | grep -E '"(Product|VendorID|ProductID|ReportDescriptor|PrimaryUsage|PrimaryUsagePage)"'
```

Keep serial numbers out of anything committed.

## Reading a crash

Crashes are saved to the `coredump` partition. With the board on USB, run
`idf.py -p PORT coredump-info` using the build that crashed. `reset_reason` 4 in
`u2b.py status` means a panic. After a bad OTA the device reports the previous version,
so check that the partition changed before confirming.

## Recovery

- **Download mode:** with the board unpowered, hold the button while plugging into a
  computer, then flash normally. This is in ROM, so bad firmware cannot disable it.
- **Clear Bluetooth bonds:** hold the button for 5 s while running, or run
  `tools/u2b.py forget`. Also remove the device on the Android side.
- **Lost API token:** run `mise run provision` again after deleting `.local/device.env`.
