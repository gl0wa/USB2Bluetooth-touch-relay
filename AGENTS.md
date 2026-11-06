# Agent instructions

Read `docs/progress.md` first: it records what is verified and what comes next.

## Working rules

- Distinguish verified behaviour from assumptions. Keep these four stages apart:
  USB reports received, BLE reports sent, events seen by Android, behaviour on
  screen. Do not claim a milestone without evidence.
- Follow `docs/hardware.md` for anything electrical. Never suggest connecting two
  5 V sources or two USB hosts together.
- Do not burn eFuses, enable secure boot or flash encryption, or make other
  irreversible changes without explicit approval.
- Bluetooth pairing needs a person to confirm it. Never remove existing pairings
  on the Android device, reset it, or change its persistent settings.
- `adb shell input` does not prove Bluetooth HID works. Temporary `hid` (uhid)
  devices are fine for testing the Android input stack; clean up afterwards.

## Git and secrets

This repository is public-facing.

- Commit small verified milestones. Never force-push, rewrite history, or push
  without approval. Do not change global Git configuration.
- Never read, print or commit credentials. Wi-Fi settings and the API token live
  only in the device `devcfg` partition and the git-ignored `.local/` directory.
- Before committing, review the staged diff and run `mise run scan`. Keep out
  personal details, local paths and device addresses.

## Commands

`mise run test`, `build`, `flash`, `monitor`, `provision`, `ota`, `scan`.
Device access goes through `tools/u2b.py`.

## Code notes

- `components/touchcore` is pure C without ESP-IDF, and must stay host-testable.
- Do not change the BLE report map without re-reading `docs/android-compat.md`.
- The USB port is normally taken by the touchscreen, so a firmware change must
  keep OTA working. Treat `main/netsvc.c` and the rollback logic with care.
