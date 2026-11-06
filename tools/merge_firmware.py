#!/usr/bin/env python3
"""Merge a build directory into one flash image at offset 0.

Reads offsets from <build_dir>/flasher_args.json (never hardcoded) and runs:
  esptool.py merge_bin -o <output> <offset> <file> ...

Includes bootloader, partition table, ota_data_initial and app, so a device
previously running from ota_1 boots the new image.

Usage:
  python3 tools/merge_firmware.py --build-dir build-release \
      --output build-release/u2b-touch-merged.bin
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir", default="build", help="IDF build directory")
    p.add_argument("--output", default=None, help="merged output binary")
    args = p.parse_args()

    build = Path(args.build_dir)
    flasher = build / "flasher_args.json"
    if not flasher.exists():
        sys.exit(f"missing {flasher}; build first")
    info = json.loads(flasher.read_text())
    flash_files = info.get("flash_files", {})
    if not flash_files:
        sys.exit(f"no flash_files in {flasher}")

    out = Path(args.output) if args.output else (build / "u2b-touch-merged.bin")
    cmd = ["esptool.py", "--chip", "esp32s3", "merge_bin", "-o", str(out)]
    for offset in sorted(flash_files, key=lambda o: int(o, 0)):
        rel = flash_files[offset]
        f = build / rel if not Path(rel).is_absolute() else Path(rel)
        if not f.exists():
            sys.exit(f"missing input {f} for offset {offset}")
        cmd += [offset, str(f)]
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)
    print(f"Wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
