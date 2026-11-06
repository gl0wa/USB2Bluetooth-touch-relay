# Hardware

## Safety rules

1. **Never feed the 5V pin while the USB-C socket is connected to a computer or
   charger.** The board has no diode between them (see below), so this ties two
   5 V supplies together and can back-feed the computer.
2. **Never connect two USB hosts to D+/D-.** Disconnect the computer before
   connecting the monitor.
3. Before feeding 5 V into a chain that reaches the monitor, confirm the monitor
   is not driving VBUS on that port (see "Checking VBUS").
4. No GPIO on the ESP32-S3 tolerates 5 V.

## StampS3 findings

From the official schematic `Sch_M5StampS3_v0.2.pdf`. **The revision of the actual
board is not yet confirmed**; a v0.3.3 schematic exists but could not be retrieved.
Re-check this section if the board is a StampS3A or a later revision.

| Item | Finding |
| --- | --- |
| USB D-/D+ | GPIO19/GPIO20, through common-mode choke L5 to the USB-C socket only |
| On headers? | No. Test pads JP2/JP3 reach them on the board; there is no header pin |
| CC1, CC2 | 5.1 kΩ to GND each (Rd): the socket always presents as a sink/UFP |
| VBUS to 5V pin | VBUS -> F1 (6 V/1 A PPTC fuse) -> `VIN_5V` -> header pin 13. **No diode** |
| Regulator | `VIN_5V` feeds the 3.3 V regulator |
| User button | GPIO0 (also the boot strap), active low |
| RGB LED | WS2812 on GPIO21 |

Consequences:

- The monitor cannot be wired to the headers: D+/D- are only on the USB-C socket.
  The computer and the monitor therefore can never be attached at once, which is
  why firmware updates go over Wi-Fi.
- 5 V on pin 13 appears on the socket's VBUS through F1, so the StampS3 can power a
  USB device from that socket. F1 limits the current to about 1 A.
- USB host is a firmware role. The Rd resistors only signal power direction to
  a USB-C partner; they do not stop the ESP32-S3 acting as host.

## Monitor

Identity and USB characteristics are **not yet verified**. Unknown: VID/PID, HID
descriptor, whether the touch controller sits behind a hub, and how the touch
port behaves on VBUS and CC. Inspect it from the Mac first (`docs/development.md`).

## Connection options

Nothing below has been tested yet. Option A costs nothing and is safe to try first.

### Option A: plain USB-C cable, powered by the monitor

```
Monitor touch port --USB-C to USB-C cable-- StampS3 USB-C socket
```

Both ends presenting Rd means nothing happens, and no harm is done. It only works if
the monitor port is dual-role, in which case it sees the StampS3's Rd, turns on
VBUS and powers the board. The StampS3 can still act as USB host, because the
power and data roles are independent at this level. The open question is whether
the monitor connects its touch controller while acting as the power source.

Test: monitor on its own power, StampS3 flashed, no other connections. If the LED
lights, the monitor is sourcing VBUS.

### Option B: standard adapters, 5 V into the headers (expected to work)

```
5 V supply + ---- pin 13 (5V)
5 V supply - ---- pin 18 or any GND pin

StampS3 USB-C socket
  -- USB-C plug to USB-A socket OTG adapter
  -- USB-A to USB-C cable
  -- Monitor touch port
```

No cable is modified and nothing needs soldering. The USB-A to USB-C cable has the
56 kΩ Rp pull-up inside its plug, so the monitor sees an ordinary host and acts as
a sink. VBUS comes from pin 13 through F1.

For the 5 V supply, use a USB charger with a USB-C breakout or a cut USB cable,
wired to the header pins.

Do Option A's test first. If the monitor sources VBUS, do not use Option B until
this has been reviewed.

### Option C: OTG adapter with a charging input

A USB-C OTG adapter with a separate power input feeds VBUS from a charger to both
the StampS3 socket and the USB-A socket, so no header wiring is needed. Behaviour
varies by adapter; check how it handles CC before relying on one.

## Checking VBUS

With the monitor powered, the chain connected, and **no 5 V supply attached**,
measure pin 13 to GND on the StampS3. Expect about 0 V. Anything above about 1 V
means the monitor drives VBUS: do not attach a supply, and use Option A instead.
Without a multimeter, the StampS3 LED lighting up gives the same warning.
