# CLAUDE.md — EthAir

Guidance for Claude Code (and humans) working in this repository.

## What this is

ESP32‑S3 firmware (M5Stack Cardputer ADV, or a bare ESP32‑S3 board with one button) that turns a
device's USB host port into a USB Ethernet link (TinyUSB CDC‑NCM) and bridges it at layer 2 to the
adapter's own Wi‑Fi access point. Built for Blackmagic cameras (Micro Studio Camera 4K G2 tested)
but device‑neutral. UI and docs are English.

- Network: adapter `192.168.73.1`, USB device always `192.168.73.2`, Wi‑Fi clients `.10–.50`.
- DHCP hands out **no gateway and no DNS** (tested: not needed; keeps iOS on mobile data). The
  DhcpServer can send option 3 to Wi‑Fi clients but it is off; the USB device never gets it.
- Defaults: SSID `EthAir-XXXX` (last two MAC bytes), password `ethair123`, channel 6.

Sibling project: **camctrl-cardputer** (`../camctrl-cardputer`) is a fork that adds a camera remote
UI on the Cardputer. See "Keeping the two firmwares in sync" below.

## Build, test, flash

```bash
~/.platformio/penv/bin/pio test -e native        # unit tests for src/logic (must stay green)
~/.platformio/penv/bin/pio run -e cardputer      # also: -e generic (no screen)
tools/flash.sh [cardputer|generic] [--usb]       # OTA when 192.168.73.1 answers, else USB serial
```

- ESP‑IDF 6.1 through PlatformIO (`platform = espressif32@8.0.0`, `framework = espidf`). Not
  Arduino: Arduino 2.0.17 has no TinyUSB network class. Managed components come from
  `src/idf_component.yml` (esp_tinyusb). Delete `sdkconfig.<env>` after editing
  `sdkconfig.defaults`.
- `CMakeLists.txt` pins `PROJECT_VER` (a repo without commits breaks IDF's git describe).

### Flashing paths (USB is a network card while the firmware runs)

1. **OTA** (`POST /api/ota`, header `X-EthAir: 1`, body = `firmware.bin`). The image confirms
   itself only after 30 s **and** one served HTTP request; unconfirmed after 90 s it restarts and
   the bootloader rolls back. `tools/flash.sh` waits for `"ota_pending":false`.
2. **Safe mode** (`POST /api/reboot?mode=safe`): USB becomes the serial/JTAG port for 3 min;
   then `tools/flash.sh … --usb`.
3. **ROM download** by hand: unplug, power switch off, hold G0 while plugging the USB cable in (with the cable already in, the switch does not cut power). After a serial flash from this
   state a USB reset does not re‑read the G0 strap → power‑cycle afterwards.

When the board sits in a camera, reach it through a phone on its Wi‑Fi with adb:
`adb push firmware.bin /data/local/tmp/` then `adb shell curl -H "X-EthAir: 1" --data-binary
@/data/local/tmp/firmware.bin http://192.168.73.1/api/ota`. Android may hop back to a known Wi‑Fi
with internet (Samsung "switch to better Wi‑Fi"); `adb shell cmd wifi connect-network <ssid> wpa2
<pass>` reconnects.

### Releasing

Bump `PROJECT_VER` in `CMakeLists.txt`, run `tools/release.sh` (native tests, every device env,
`dist/*-full.bin` for USB at 0x0 + `dist/*-ota.bin` + `SHA256SUMS`), commit, tag `vX.Y.Z`, attach
the `dist/` files to the GitHub release. The version shows on the settings page and in
`/api/status`. `tools/release.sh` is identical in both firmwares.

## Architecture

| File | Role |
|---|---|
| `src/bridge.cpp` | L2 forwarder: TinyUSB NCM ↔ Wi‑Fi AP (`esp_wifi_internal_*`) ↔ own lwIP netif. Own NCM config descriptor (no remote wakeup), queued USB TX task, USB re‑attach watchdog |
| `src/dhcp_task.cpp` | DHCP server on the netif (`logic/dhcp_server`) |
| `src/mdns_task.cpp` | optional mDNS fallback responder (off by default) |
| `src/web.cpp` | settings page, `/api/status`, `/api/log`, `/api/reboot`, `/api/ota`; Host/Origin/`X-EthAir` guards |
| `src/boot_mode.cpp` | safe mode, USB PHY handback to serial/JTAG, ROM download restart |
| `src/settings_store.cpp` | NVS namespace `ethair` |
| `src/display.cpp`, `src/button.cpp` | Cardputer status screen (esp_lcd + glcdfont), G0 factory reset |
| `src/logic/*` | pure C++ (no ESP‑IDF): forwarder, DHCP, mDNS codec, settings, web text, boot guard, frame info |

Rules: anything testable goes in `src/logic` with a native test under `test/`; hardware glue stays
thin. No serial console while NCM runs — use `/api/log` (ring buffer).

## Hard‑won hardware facts (do not regress)

- **Cardputer + camera: the Cardputer's power switch must be OFF.** With the battery on it puts
  5 V on USB‑C and the Blackmagic camera never starts its USB host. After 15 s without a host the
  screen tells the user (Cardputer build only). PCs don't care.
- **No remote wakeup in the NCM descriptor.** With it, the camera's Linux autosuspends the idle
  adapter and frames to the camera are lost.
- **No VBUS sensing**: a pulled cable only shows up as a bus suspend. A link suspended ≥ 3 s is
  shown as down; re‑attach (D+ toggle) after 5 s unmounted / 10 s suspended.
- **USB PHY handback** (safe mode/download): route the PHY to serial/JTAG, turn off D−/D+ GPIO
  pull‑ups, pulse all pulls off with the pad enabled, then set `dp_pu = 1` *before* releasing the
  override (USJ registers survive `esp_restart`), stop the OTG controller.
- **Android 17**: apps need the *Nearby devices* permission (`ACCESS_LOCAL_NETWORK`) for LAN
  access, even target‑36 apps (Chrome failed with `ERR_TOO_MANY_RETRIES`). A gateway is not needed.
- **A USB host computer must never get a gateway** from the adapter (it once took over a dev PC's
  default route and cut its internet).
- The boot guard counts crashes per firmware image (first 4 bytes of the ELF SHA): after a
  reflash the old crash count no longer forces safe mode.
- Low heap kills the bridge silently (frames can't be allocated): firmware restarts if free heap
  stays < 12 KB (or the largest free block < 4 KB) for 20 s; `/api/status` reports `min_free_heap`.

## Keeping the two firmwares in sync

`../camctrl-cardputer` was forked from this repo. Files that are (nearly) identical and should get
the same fixes in both: `src/bridge.*`, `src/dhcp_task.*`, `src/mdns_task.*`, `src/boot_mode.*`,
`src/log_buffer.*`, `src/settings_store.*` (the fork adds UI flags), `src/web.*`,
`src/logic/{forwarder,dhcp_server,mdns,settings,web_text,boot_guard,frame_info,hold_detector}*`,
`partitions.csv`, `tools/flash.sh`, `tools/release.sh`, and the corresponding tests. Port fixes deliberately (diff the
files); they are not a shared library on purpose, so each firmware builds and evolves on its own.

## Conventions

- Commit messages in English, imperative subject; no `Co-Authored-By`. No usernames, home paths
  or other personal data in code, docs or commits (`git grep /home/` before pushing).
- Remote: `git@github.com:koorchik/ethair.git` (private).
- Design notes: `docs/superpowers/specs/`.
