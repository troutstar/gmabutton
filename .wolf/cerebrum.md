# Cerebrum

> OpenWolf's learning memory. Updated automatically as the AI learns from interactions.
> Do not edit manually unless correcting an error.
> Last updated: 2026-05-16

## User Preferences

- **Flash workflow — always two separate esptool commands:** (1) `idf.py merge-bin -o <absolute_path>_merged.bin` then flash merged at `0x0`, (2) flash `gmabutton1_config.bin` (or `gmabutton2_config.bin`) at `0x9000` as a separate esptool call. Never combine them in one write_flash — the merged binary covers 0x9000 and esptool errors with "Detected overlap".
- **ESP-IDF v5.5.3 Windows one-shot PowerShell:** `Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; $env:IDF_TOOLS_PATH = "C:\Espressif"; . "C:\Espressif\frameworks\esp-idf-v5.5.3\export.ps1" 2>&1 | Out-Null; idf.py ...`

## Key Learnings

- **Project:** gmabutton
- **Deployed devices:** gmabutton1=COM5 (MAC a4:f0:0f:8d:1d:e0, WiFi 192.168.86.164, WG 10.8.0.4), gmabutton2=COM7 (WiFi 192.168.86.163, WG 10.8.0.5). NVS credentials in `flashtool/firmware/gmabutton1_config.bin` and `gmabutton2_config.bin`.
- **Flash artifact:** `flashtool/firmware/gmabutton.bin` is a RAW APP binary — do NOT flash at 0x0. Run `idf.py merge-bin -o flashtool/firmware/gmabutton_merged.bin` first. Flash merged at 0x0, then NVS config at 0x9000.
- **gmabutton architecture:** ESP32 CYD pair. Hold ~1.5s = blue "call me", hold ~3.5s = red "help me". Receiver flashes screen+LED until acknowledged. Dismiss: ~0.5s hold → overlay → quick tap. 10s hold = factory reset / SoftAP wizard at 192.168.4.1. WireGuard client-only to 192.168.86.5:51820. SNTP UTC.
- **LED hardware:** GPIO 4 (led_blue, B channel) dead on deployed boards. GPIO 17 (led_red, R channel) is the only working LED colour (actually blue on hardware). Both CALL and HELP receiver flash use led_red().
- **HTTP propagation rule:** /api/alert and /api/dismiss apply state locally ONLY — they do NOT call peer_send(). Only gesture-initiated events propagate. Prevents ping-pong loops.

## Do-Not-Repeat

- [2026-05-17] Do NOT flash `gmabutton.bin` (raw app) at 0x0 — overwrites bootloader, device bricks with `invalid header: 0x22294745`. Always use the merged binary.
- [2026-05-17] Do NOT combine merged binary + NVS config in one `esptool write_flash` command — errors with "Detected overlap at address: 0x9000". Use two separate esptool calls.
- [2026-05-17] Do NOT pass a relative path to `idf.py merge-bin -o` — esptool cds into build/ first, making the path wrong. Use absolute path.

## Decision Log

<!-- Significant technical decisions with rationale. Why X was chosen over Y. -->
