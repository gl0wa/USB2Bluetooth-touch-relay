# Android TV compatibility

Tested on an NVIDIA SHIELD TV (tube model): Android 11, API 30, kernel 4.9, no
root. `android.hardware.touchscreen` is not declared, but `hid-multitouch`, `uhid`
and `hid-generic` are all in the kernel.

## Method

Android's Bluetooth HID host passes reports to the kernel through `/dev/uhid`. The
`hid` shell command creates a temporary uhid device, so `adb shell` can test the
kernel and InputReader path without any hardware. It exercises the descriptor
handling only: it does not prove the BLE transport.

## Results

| Descriptor | Kernel driver | InputReader result |
| --- | --- | --- |
| Touch Screen, Tip Switch + X/Y, no Contact ID | `hid-generic` | `DeviceType: pointer`, mode pointer. Behaves like a touchpad |
| Touch Screen with Contact ID, Contact Count, Contact Count Max | `hid-multitouch` | `INPUT_PROP_DIRECT`, `DeviceType: touchScreen`, mode direct, source `0x1002` |

The second descriptor delivered a tap as `ABS_MT_*` and `BTN_TOUCH` events. It is
used as the BLE report map, in `main/ble_hogp.c`, and pinned in the host tests.
**Keep Contact ID in the descriptor**: an IDC file could override the device type,
but installing one needs root.

## Open questions

- Behaviour over real BLE (HOGP) is untested.
- Under uhid the device took about 5.5 s to appear and reported 10 slots instead of
  the 1 declared. This suggests the GET_REPORT for Contact Count Max timed out,
  possibly a limitation of the `hid` tool. On BLE the Feature report is served by
  a GATT read; check whether the delay goes away.
- Whether Android TV launcher and app UIs respond usefully to direct touch.
