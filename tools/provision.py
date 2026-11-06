#!/usr/bin/env python3
"""Writes Wi-Fi settings and an API token to the `devcfg` partition over USB.

Bluetooth bonds live in the separate `nvs` partition and are left untouched.
The token is saved to .local/device.env, which git ignores. The Wi-Fi password
is read from U2B_WIFI_PASS or a prompt and is never stored on this computer.
"""
import argparse
import getpass
import os
import secrets
import subprocess
import sys
import tempfile
from pathlib import Path

DEVCFG_OFFSET = "0x12000"
DEVCFG_SIZE = "0x6000"
ROOT = Path(__file__).resolve().parent.parent
LOCAL_ENV = ROOT / ".local" / "device.env"


def read_env():
    values = {}
    if LOCAL_ENV.exists():
        for line in LOCAL_ENV.read_text().splitlines():
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="serial port, for example /dev/cu.usbmodem101")
    parser.add_argument("--ssid", default=os.environ.get("U2B_WIFI_SSID"))
    args = parser.parse_args()

    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        sys.exit("IDF_PATH is not set; run this through `mise run provision`")

    ssid = args.ssid or input("Wi-Fi SSID: ").strip()
    password = os.environ.get("U2B_WIFI_PASS")
    if password is None:
        password = getpass.getpass("Wi-Fi password (empty for an open network): ")
    if not ssid or len(ssid) > 32 or len(password) > 63:
        sys.exit("SSID must be 1-32 bytes and the password at most 63")

    env = read_env()
    token = env.get("U2B_TOKEN") or secrets.token_urlsafe(32)
    LOCAL_ENV.parent.mkdir(mode=0o700, exist_ok=True)
    fd = os.open(LOCAL_ENV, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(f"U2B_HOST={env.get('U2B_HOST', 'u2b-touch.local')}\nU2B_TOKEN={token}\n")

    generator = Path(idf_path) / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
    with tempfile.TemporaryDirectory() as tmp:
        csv_path, bin_path = Path(tmp) / "devcfg.csv", Path(tmp) / "devcfg.bin"
        rows = ["key,type,encoding,value", "net,namespace,,", f"ssid,data,string,{ssid}", f"token,data,string,{token}"]
        if password:
            rows.append(f"pass,data,string,{password}")
        csv_path.write_text("\n".join(rows) + "\n")
        csv_path.chmod(0o600)
        subprocess.run([sys.executable, str(generator), "generate", str(csv_path), str(bin_path), DEVCFG_SIZE],
                       check=True, stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", args.port,
                        "write_flash", DEVCFG_OFFSET, str(bin_path)], check=True)
    print(f"Provisioned. API token stored in {LOCAL_ENV.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
