# User guide

This page is for someone who wants a working device, not for developers.
For build, OTA and Wi-Fi details, see [development](development.md).

## What it is

A small gateway (M5Stack StampS3, ESP32-S3) that makes a USB touchscreen work
wirelessly with an Android TV that has no free USB port. It reads the panel as a
USB host and presents it over Bluetooth LE as an absolute touchscreen, so Android
needs no extra app. Single touch only.

```
Touchscreen --USB--> StampS3 --Bluetooth--> Android TV
```

Install page: `https://gl0wa.github.io/USB2Bluetooth-touch-relay/`
(flashing needs desktop Chrome or Edge; see below).

Only one monitor and one Android TV device have been tested so far.
Other screens and hosts may behave differently.

## What you need

- M5Stack StampS3 (ESP32-S3).
- A USB touchscreen monitor with a touch port.
- A data USB-C to USB-C cable (charge-only cables do not work).
- A computer with desktop Chrome or Edge for the one-time install.
- An Android TV that supports Bluetooth HID.

## Install

1. On the computer, open the install page above in desktop Chrome or Edge.
2. Unplug the StampS3 from the monitor (if connected there).
3. Connect the StampS3 to the computer with the data USB-C cable.
4. Click **Install**, pick the serial port, and wait.
5. If the port does not appear or flashing fails: unplug, **hold the button while
   plugging in**, then retry. This is ROM download mode; bad firmware cannot disable it.
6. Erasing on install removes the Bluetooth pairing, so pair again afterwards.

## Connect

1. Move the USB-C cable to the monitor's touch port, with the monitor on external power.
2. In testing the monitor powered the StampS3 from the USB-C cable alone. Other monitors
   may not; read [hardware](hardware.md) before using the 5V pin. Never feed the 5V pin
   while USB is connected to a computer or charger, and never connect two USB hosts
   to D+/D-.
3. The LED shows the state (see below). When it blinks yellow, the gateway is waiting
   for Bluetooth.

## Pair

On the Android TV: Settings → Remotes & Accessories → Add accessory → **“Usb2BT Touch”**.
If pairing fails, try disconnecting other Bluetooth audio while pairing; this helped once,
cause unknown. Also remove the gateway on the Android side before re-pairing after
a mode change or re-flash.

## How to use it

Taps land in place with no calibration, one-finger dragging works, and Settings,
apps and video scrubbing respond to touch.

### Navigation: Back, Home, D-pad mode

Android TV has no on-screen Back or Home:

- Swipe in from the **LEFT** edge (moving right) for **Back**.
- Swipe up from the **BOTTOM** edge for **Home**.
- A short press of the StampS3 button is Back as a fallback.
- Edge taps and drags still work as touches, only slightly late.

Some controls ignore touch (for example the launcher's top tabs). A **D-pad mode**
covers them: swipe **DOWN from the TOP edge** to toggle it. In D-pad mode swipes
become arrow keys and a tap becomes Select (a long drag repeats). To leave, swipe down
from the top 8% again, or wait 10 s with no contact. A slower exit swipe up to 0.8 s
is fine; Down swipes that should move focus must start lower.

### LED

| LED | Meaning |
| --- | --- |
| Blue pulse | Maintenance: USB belongs to a computer, no USB host |
| Red blink | No touch device |
| Yellow blink | Waiting for Bluetooth |
| Green | Relaying touches |
| Cyan | D-pad mode (takes priority over green) |
| Purple (brief) | Bluetooth bonds cleared after a 5 s button hold |

## Settings

Plug the StampS3 into a computer (blue LED), open the install page's Settings section,
click **Connect**, then **Read status**. Every response is one line starting with
`U2B` followed by JSON, so the page can filter it from log output.

- **Gestures on (default):** Android TV and other screens with no on-screen Back or Home.
- **Gestures off:** when the target has its own navigation, or when you need touches at
  the screen edges to respond with no delay at all, for example a kiosk or a drawing app.
  Off also disables D-pad mode and the button's Back.
- **Bluetooth mode, Android (default):** needed for Back, Home and D-pad. It uses a
  non-standard report format verified only on one Android TV device.
- **Bluetooth mode, Standard:** try this on devices that are not Android, or if touch
  misbehaves after pairing, such as Windows, Linux or iPad. Touch only, no navigation keys.
  Needs a reboot and re-pair. No device other than the tested one has been tried, so these
  are expectations, not tested results.
- **Forget pairings:** moving the gateway to another TV, or when it will not reconnect.
  Also remove the device on the Android side. Asks for confirmation.
- **Reboot:** applies a Bluetooth-mode change (re-pair too).

`hid` changes take effect after reboot and re-pair; the status shows
`reboot_required` when the saved mode differs from the running one. In standard mode
gestures are forced off (the saved gesture setting is kept).

## Troubleshooting

- **Couldn't pair:** disconnect other Bluetooth audio while pairing; this helped once,
  cause unknown. Then retry, and remove the gateway on the Android side first.
- **No touch:** check the LED. Red blink means no touch device (cable, monitor power,
  touch port). Yellow blink means waiting for Bluetooth (pair again). Green means relaying.
- **Some buttons need the remote:** use D-pad mode (top-edge swipe); some Android TV
  controls still need it.
- **Wrong touches at edges:** turn gestures off if the target has its own navigation
  or needs edge touches with no delay.
- **Re-flash:** use the install page again (desktop Chrome or Edge). Hold the button
  while plugging in if the port does not appear.

## Known limitations

- Single touch only.
- Some controls still need D-pad mode.
- Tested combination: one monitor, one Android TV device. Anything else is untested.
- This build has no over-the-air updates; update by re-flashing from the install page.
- In testing the monitor powered the StampS3 from the USB-C cable with the monitor on
  external power. Whether other monitors do, and cold-start behaviour, is untested.
  Read [hardware](hardware.md) before using the 5V pin.
