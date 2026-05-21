# Cerebrum

> OpenWolf's learning memory. Updated automatically as the AI learns from interactions.
> Do not edit manually unless correcting an error.
> Last updated: 2026-05-16

## User Preferences

- **Flash confirmation protocol:** Ask the user once ("Ready to flash both devices?"). When they say yes, that covers the entire sequence — build, merge, copy, flash COM5 merged, flash COM5 NVS, flash COM7 merged, flash COM7 NVS. Do NOT pause mid-sequence for additional tool permission prompts. The upfront "yes" is the only confirmation needed.
- **Flash workflow — ALWAYS flash BOTH merged binary AND NVS every time:** merged at 0x0, then NVS at 0x9000. Never skip the NVS step — the merged binary erases 0x9000 so credentials are wiped without it.
- **Flash workflow — always two separate esptool commands:** (1) `idf.py merge-bin -o <absolute_path>_merged.bin` then flash merged at `0x0`, (2) flash `gmabutton1_config.bin` (or `gmabutton2_config.bin`) at `0x9000` as a separate esptool call. Never combine them in one write_flash — the merged binary covers 0x9000 and esptool errors with "Detected overlap".
- **ESP-IDF v5.5.3 Windows one-shot PowerShell:** `Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; $env:IDF_TOOLS_PATH = "C:\Espressif"; . "C:\Espressif\frameworks\esp-idf-v5.5.3\export.ps1" 2>&1 | Out-Null; idf.py ...`

## Key Learnings

- **Project:** gmabutton
- **Deployed devices:** gmabutton1=COM5 (MAC a4:f0:0f:8d:1d:e0, WiFi 192.168.86.164, WG 10.8.0.4), gmabutton2=COM7 (MAC e0:8c:fe:32:e7:3c, WiFi 192.168.86.163, WG 10.8.0.5). NVS credentials in `gmabutton1_nvs.bin` and `gmabutton2_nvs.bin` (project root). Flash at 0x9000 as a separate esptool call after the merged binary.
- **Flash artifact:** `build/gmabutton.bin` is a RAW APP binary — do NOT flash at 0x0. Run merge-bin manually via `python -m esptool --chip esp32 merge_bin -o gmabutton_merged.bin -f raw --flash_mode dio --flash_freq 40m --flash_size 2MB 0x1000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\gmabutton.bin` (output goes to project root). Flash merged at 0x0, then NVS config at 0x9000. Do NOT use `idf.py merge-bin -o <path>` — esptool cds into build/ and the output dir must already exist.
- **gmabutton architecture:** ESP32 CYD pair. Hold ~1.5s = blue "call me", hold ~3.5s = red "help me". Receiver flashes screen+LED until acknowledged. Dismiss: ~0.5s hold → overlay → quick tap. 10s hold = factory reset / SoftAP wizard at 192.168.4.1. WireGuard client-only to 192.168.86.5:51820. SNTP UTC.
- **LED hardware:** GPIO 4 (led_blue, B channel) dead on deployed boards. GPIO 17 (led_red, R channel) is the only working LED colour (actually blue on hardware). Both CALL and HELP receiver flash use led_red().
- **HTTP propagation rule:** /api/alert and /api/dismiss apply state locally ONLY — they do NOT call peer_send(). Only gesture-initiated events propagate. Prevents ping-pong loops.
- **Timezone:** User is in California. TZ string `PST8PDT,M3.2.0,M11.1.0` set in `wifi_manager.c` `start_sta()`. SNTP syncs UTC from pool.ntp.org; `localtime_r` applies the TZ offset.

## Do-Not-Repeat

- [2026-05-17] Do NOT flash `gmabutton.bin` (raw app) at 0x0 — overwrites bootloader, device bricks with `invalid header: 0x22294745`. Always use the merged binary.
- [2026-05-17] Do NOT combine merged binary + NVS config in one `esptool write_flash` command — errors with "Detected overlap at address: 0x9000". Use two separate esptool calls.
- [2026-05-17] Do NOT pass a relative path to `idf.py merge-bin -o` — esptool cds into build/ first, making the path wrong. Use absolute path.

## Decision Log

- [2026-05-21] Screensaver replaced with noof port. The `s_fb` strip buffer is NOT persistent across frames (reused for both strips each render cycle), so GL-style accumulation is impossible without a separate full-frame buffer. Chose trail-history approach (TRAIL_LEN=5 positions per shape) instead. noof's GL alpha-blended fill is skipped; only the colored line-loop outline is drawn (4 lines per blade). Velocity scaled ×8 over noof's defaults to compensate for ~80ms ESP32 frame vs noof's 10ms target.

<!-- Significant technical decisions with rationale. Why X was chosen over Y. -->
