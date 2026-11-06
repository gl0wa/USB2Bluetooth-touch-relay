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
| Touch Screen with Contact ID and Contact Count, no Feature report | `hid-multitouch` | `INPUT_PROP_DIRECT`, `DeviceType: touchScreen`, mode direct, source `0x1002` |

The second descriptor delivered a tap as `ABS_MT_*` and `BTN_TOUCH` events. It is
used as the BLE report map, in `main/ble_hogp.c`, and pinned in the host tests.
**Keep Contact ID in the descriptor**: an IDC file could override the device type,
but installing one needs root.

## Combined touch plus Back/Home keys

The BLE report map appends a Consumer Control collection (report 2: bit0 AC
Back, bit1 AC Home, bit2 Up, bit3 Down, bit4 Left, bit5 Right, bit6 Select)
after the touch collection, with no Feature report. The map bytes changed
when the D-pad keys were added, so re-pair once after updating.

Measured over BLE with two Input Report characteristics (HCI capture): Android
discovered both but read the Report Reference and wrote the CCCD for the first
(touch) only. The keys characteristic was never subscribed, so
`ble_hogp_send_keys()` always returned false and Back/Home never reached
Android. A second Report characteristic does not carry keys on this stack.

Putting the Consumer usages inside the Touch Screen collection was also tested
through uhid: `hid-multitouch` ignored them. Do not try that again.

Current approach: one Input Report characteristic carries both reports. In
Android mode (default) its Report Reference declares Report ID 0, so the
payload carries its own first byte as report ID (touch: `[1, ...]`, keys:
`[2, bits]`). This relies on memory of AOSP `bta_hh_le_input_rpt_notify`
prepending a report ID only when it is non-zero; that was not read from source,
but the behaviour is confirmed on hardware: on the Shield, Back and Home work over
BLE and touch is unaffected. It is non-standard HOGP. Hosts other than this Android
stack may misparse it, so a standard mode exists
(`hid-mode standard`): normal report IDs, touch only, no keys.

## D-pad report map (uhid only, not yet over BLE or on screen)

The extended Consumer collection (Back/Home plus Up/Down/Left/Right/Select)
was tested through uhid only: touch stayed `touchScreen` direct mode and the
key bits reached the kernel, where Android's `Generic.kl` maps them to
`DPAD_*`. Sources were `0x1303`. This exercises the descriptor and the
kernel/InputReader path only. D-pad keys over BLE and behaviour on screen
are NOT yet verified; re-pair once after the map change before comparing.

uhid tests exercise the descriptor and the kernel/InputReader path only; they
do not exercise HOGP GATT setup (service discovery, Report Reference reads,
CCCD subscribes), so they cannot catch the missing-subscribe failure above.

## Verified over BLE

Over real BLE (bus `0x0005`) the Shield classifies the gateway as `touchScreen`,
direct mode, source `0x1002`. An injected tap and swipe (34 reports sent)
produced 34 `SYN_REPORT` events in the kernel, 2 presses, 2 releases, with exact
coordinates. After rebooting the gateway it reconnected by itself in about 3 s
with the bond kept, and touch still worked.

## Report map delay

After every Bluetooth reconnect the input device took about 6 s to appear. The
likely cause is the kernel sending GET_REPORT for Contact Count Maximum and
timing out; that is not proven. The fix is a descriptor without the Feature
report, as above. Measured over BLE after a fresh pairing, the input device now
returns about 1.2 s after being removed, down from 6-7 s, and is still classified
`touchScreen` in direct mode.

## Touch behaviour in Android TV UI

Works with single touch, user-confirmed on screen: taps land in the right
place with no calibration, one-finger dragging works, opening apps works,
Settings can be opened, video progress-bar scrubbing and play/pause work.

Does not work: Android TV is built for D-pad navigation, so some controls do
not respond to touch, for example the launcher's top tabs. There is no
on-screen Back or Home, so touch alone cannot leave an app or Settings; the
remote is still needed.

## Open questions

- Whether the Report ID 0 approach works on other Android devices.

## Pairing investigation

First attempts from the Shield failed with "couldn't pair" while it was
streaming A2DP audio to a speaker in another room; pairing succeeded after that
speaker was disconnected. The Shield's HCI log shows `LE_CreateConn`, then
`LE Connection Complete` (status 0), then a disconnect with reason `0x3E` after
about 0.3 s, and the gateway logged no connection at all. So the gateway missed
the connection setup. RSSI was about -56 dBm, and the connection interval was
50 ms. Whether the audio stream or the distance mattered is unknown; retest with
the speaker nearby. A Mac connected first time with Wi-Fi on.

mDNS name lookup got less reliable once BLE was connected.

The HCI log comes from `adb shell dumpsys bluetooth_manager` (the
`BTSNOOP_LOG_SUMMARY` section: base64, then zlib).

## Warning

Android may cache a bonded device's GATT table and report map, so after changing
the report map forget the device on the Android side and re-pair before
comparing behaviour.
