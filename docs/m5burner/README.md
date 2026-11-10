# M5Burner listing

What to enter in M5Burner (USER CUSTOM → Publish, logged in). Use the **full** image from the
release (`tools/release.sh` → `dist/`); it is flashed at 0x0.

| Field | Value |
|---|---|
| Name | EthAir |
| Version | 1.0.0 |
| Device type | Cardputer-ADV (also works on the original Cardputer; other ESP32‑S3 boards: use the `generic` image from GitHub) |
| GitHub | https://github.com/koorchik/ethair |
| Firmware | `ethair-cardputer-v1.0.1-full.bin` |
| Cover | `docs/m5burner/cover.png` (800 × 450, `tools/m5burner_cover.py`) |

## Description

**USB Ethernet to Wi‑Fi bridge for cameras and other USB hosts.**

EthAir turns the Cardputer into a USB Ethernet adapter that bridges to its own Wi‑Fi. Plug it into a Blackmagic camera (or any device whose USB host port accepts USB Ethernet adapters), join the Wi‑Fi EthAir‑XXXX (password ethair123) from a phone, and reach the device at 192.168.73.2: REST API, WebSocket, mDNS. Pairs with the CamCtrl app (camctrl.app) for full Blackmagic camera control.

IMPORTANT: set the Cardputer's power switch OFF when it is plugged into a camera; it runs from the camera's USB port.

Settings: http://192.168.73.1. Factory reset: hold G0 for 5 s. Updates over Wi‑Fi. Source and docs: https://github.com/koorchik/ethair
