#!/usr/bin/env python3
"""Talks to the gateway over Wi-Fi: status, logs, OTA and touch injection."""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

LOCAL_ENV = Path(__file__).resolve().parent.parent / ".local" / "device.env"
MAX = 32767


def config():
    values = {}
    if LOCAL_ENV.exists():
        for line in LOCAL_ENV.read_text().splitlines():
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    host = os.environ.get("U2B_HOST", values.get("U2B_HOST", "u2b-touch.local"))
    token = os.environ.get("U2B_TOKEN", values.get("U2B_TOKEN"))
    if not token:
        sys.exit("No API token; run `mise run provision` first or set U2B_TOKEN")
    return host, token


def call(path, method="GET", data=None, timeout=10):
    host, token = config()
    # The radio is shared with Bluetooth now, so the first lookup or request can
    # fail transiently. Retry reads only: repeating a POST could double-send
    # OTA, reboot, forget or touch input.
    attempts = 3 if method == "GET" else 1
    for attempt in range(attempts):
        req = urllib.request.Request(f"http://{host}{path}", method=method, data=data, headers={"X-Token": token})
        try:
            return urllib.request.urlopen(req, timeout=timeout)
        except urllib.error.HTTPError:
            raise
        except urllib.error.URLError:
            if attempt + 1 >= attempts:
                raise
            time.sleep(1)


def cmd_status(_):
    print(json.dumps(json.load(call("/status")), indent=2))


def cmd_log(args):
    since = 0
    while True:
        with call(f"/log?since={since}") as r:
            sys.stdout.write(r.read().decode("utf-8", "replace"))
            sys.stdout.flush()
            since = int(r.headers["X-Next"])
        if not args.follow:
            return
        time.sleep(0.5)


def cmd_desc(_):
    print(call("/usb/desc").read().decode(), end="")


def cmd_ota(args):
    image = Path(args.image).read_bytes()
    print(f"Uploading {len(image)} bytes")
    print(call("/ota", "POST", image, timeout=180).read().decode(), end="")
    time.sleep(args.wait)
    print("Running:", json.load(call("/status"))["version"])


def touch(x, y, down):
    call(f"/touch?x={int(x)}&y={int(y)}&down={int(down)}", "POST", b"")


def to_raw(fraction):
    return max(0, min(MAX, round(fraction * MAX)))


def cmd_tap(args):
    x, y = to_raw(args.x), to_raw(args.y)
    touch(x, y, True)
    time.sleep(args.hold)
    touch(x, y, False)


def cmd_swipe(args):
    ms = int(args.duration * 1000)
    call(f"/swipe?x1={to_raw(args.x1)}&y1={to_raw(args.y1)}&x2={to_raw(args.x2)}&y2={to_raw(args.y2)}&ms={ms}",
         "POST", b"", timeout=10 + args.duration)


def cmd_wifi_pause(args):
    print(call(f"/wifi-pause?s={args.seconds}", "POST", b"").read().decode(), end="")


def cmd_gestures(args):
    print(call(f"/config?gestures={1 if args.state == 'on' else 0}", "POST", b"").read().decode(), end="")


def cmd_key(args):
    print(call(f"/key?k={args.which}", "POST", b"").read().decode(), end="")


def cmd_post(args):
    print(call(f"/{args.command}", "POST", b"").read().decode(), end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    p = sub.add_parser("log")
    p.add_argument("-f", "--follow", action="store_true")
    p.set_defaults(fn=cmd_log)
    sub.add_parser("desc", help="print USB HID report descriptors").set_defaults(fn=cmd_desc)
    p = sub.add_parser("ota")
    p.add_argument("image")
    p.add_argument("--wait", type=float, default=12)
    p.set_defaults(fn=cmd_ota)
    p = sub.add_parser("tap", help="coordinates are fractions of the screen, 0..1")
    p.add_argument("x", type=float)
    p.add_argument("y", type=float)
    p.add_argument("--hold", type=float, default=0.08)
    p.set_defaults(fn=cmd_tap)
    p = sub.add_parser("swipe", help="coordinates are fractions of the screen, 0..1")
    for name in ("x1", "y1", "x2", "y2"):
        p.add_argument(name, type=float)
    p.add_argument("--duration", type=float, default=0.3)
    p.set_defaults(fn=cmd_swipe)
    sub.add_parser("reboot").set_defaults(fn=cmd_post)
    sub.add_parser("forget", help="clear all Bluetooth bonds").set_defaults(fn=cmd_post)
    sub.add_parser("confirm", help="accept the running image, cancelling rollback").set_defaults(fn=cmd_post)
    p = sub.add_parser("wifi-pause", help="turn Wi-Fi off for a while, to test Bluetooth alone")
    p.add_argument("seconds", type=int)
    p.set_defaults(fn=cmd_wifi_pause)
    p = sub.add_parser("gestures", help="edge-swipe Back/Home on or off, persists across reboot")
    p.add_argument("state", choices=("on", "off"))
    p.set_defaults(fn=cmd_gestures)
    p = sub.add_parser("key", help="send a Bluetooth Back/Home key directly, bypasses the switch")
    p.add_argument("which", choices=("back", "home"))
    p.set_defaults(fn=cmd_key)
    args = parser.parse_args()
    try:
        args.fn(args)
    except urllib.error.URLError as e:
        sys.exit(f"Request failed: {e}")


if __name__ == "__main__":
    main()
