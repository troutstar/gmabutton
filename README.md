# gmabutton

Paired alert system for two ESP32-2432S028 ("Cheap Yellow Display") boards. Hold the
touchscreen to send a call or help signal to the other device. Works over local WiFi
with an optional WireGuard VPN tunnel for cross-network use.

---

## Hardware

**ESP32-2432S028 ("Cheap Yellow Display")**
- ESP32-D0WD-V3, dual core, 240 MHz
- ILI9341 320×240 display, XPT2046 touch
- RGB LED (note: on deployed boards GPIO 17 is the only working LED colour)
- 4 MB flash

You need two of these. They are cheap — ~$10–15 on AliExpress.

---

## Gestures

| Hold duration | Action |
|---------------|--------|
| ~1.5 s | Blue "call me" signal |
| ~3.5 s | Red "help me" signal |
| ~0.5 s (while alert showing) | Open dismiss overlay |
| Quick tap on overlay | Confirm dismiss |
| 10 s | Factory reset (wipes NVS, returns to Setup Mode) |

While holding, an expanding circle animates on screen: blue grows to fill the screen at
the call threshold, then red expands over it toward the help threshold. Gesture fires on
release.

---

## Architecture

- **Core 0** — FreeRTOS system tasks, WiFi, WireGuard (lwIP)
- **Core 1** — render task (display + touch + screensaver + alert overlay)
- **Transport** — HTTP POST to peer IP (`/api/alert`, `/api/dismiss`)
- **WireGuard** — optional VPN client via `trombik/esp_wireguard` 0.9.0
- **Storage** — NVS namespace `gmabutton`, all config persisted across reboots

---

## Build

Requires ESP-IDF v5.5.3 on Windows. See [`FLASH.md`](FLASH.md) for the exact
PowerShell commands with all required environment setup.

```powershell
# Quick summary — see FLASH.md for full detail
idf.py build
idf.py merge-bin -o C:\path\to\output_merged.bin
```

Always use `merge-bin` after build to produce a single binary (bootloader + partition
table + app) that flashes at address `0x0`. Flashing the raw app binary at `0x0`
overwrites the bootloader and bricks the device.

---

## Flashing

### Option A — Flash tool (recommended)

A local web UI for flashing is in `../flashtool/`. Start it:

```powershell
cd ..\flashtool
pip install -r requirements.txt
python flashtool.py
```

Then open `http://127.0.0.1:5757`. Drop merged `.bin` files into `flashtool/firmware/`.

**For a fresh device or full reflash:** use Single bin mode, select the merged binary,
set address `0x0`, pick the COM port, flash.

**Restoring a known device with its saved config:** use Multiple bins mode and flash two
files in one shot — the merged firmware at `0x0` and the device's NVS config partition
at `0x9000`. This bypasses the setup wizard entirely. See
[Device config files](#device-config-files) below.

### Option B — idf.py direct

```powershell
idf.py -p COM5 flash
```

See [`FLASH.md`](FLASH.md) for the full environment preamble required on Windows.

---

## First-time setup (SoftAP wizard)

After flashing a device with no prior config:

1. Screen shows **SETUP MODE** — AP SSID: `gmabutton-XXXX` (last 4 hex digits of MAC)
2. Connect phone/laptop to that open network
3. Browse to `http://192.168.4.1/`
4. Fill in the form:
   - **WiFi SSID / Password** — your network
   - **Device name** — e.g. `gmabutton1`
   - **Peer IP** — the *other* device's IP on your network (or WireGuard tunnel IP)
   - **Peer port** — `8121` (default)
   - **WireGuard** — optional; leave blank to use plain WiFi only
5. Submit → device saves and reboots into normal mode

For two-device setups: flash and configure device A first, note its IP, then configure
device B with that IP as its peer. Then update device A's peer IP to device B's address
via `http://<A-ip>/config`.

---

## WireGuard

Both devices act as WireGuard clients. The VPN server must be running separately (not
part of this project). Config entered in the wizard:

| Field | Example |
|-------|---------|
| Server endpoint | `192.168.86.5` |
| Server port | `51820` |
| Server public key | (base64, from your WireGuard server config) |
| Device private key | (base64, generated for each device on your server) |
| Local tunnel IP | `10.8.0.4` / `10.8.0.5` |

Once WireGuard is connected, update peer IPs to the tunnel addresses (`10.8.0.x`) so
alerts work across networks without depending on LAN IPs.

---

## Device config files

Device-specific credentials (WiFi password, WireGuard private keys) are **not stored in
this repo** — they are gitignored. They live as:

| File | Purpose |
|------|---------|
| `DEVICE_CONFIG.md` | Human-readable reference table for all fields |
| `gmabutton1_nvs.csv` | Source for device 1 NVS partition binary |
| `gmabutton2_nvs.csv` | Source for device 2 NVS partition binary |

The corresponding `.bin` files are generated with `nvs_partition_gen.py` and dropped
into `flashtool/firmware/` for use with the flash tool. `DEVICE_CONFIG.md` contains the
regeneration command.

To create these files from scratch for new devices: run through the SoftAP wizard on
each device, then dump and parse the NVS partition:

```powershell
# Dump NVS from a running device
parttool.py -p COM5 -b 460800 read_partition --partition-name nvs --output device_nvs.bin

# Parse and display all stored keys
python C:\Espressif\frameworks\esp-idf-v5.5.3\components\nvs_flash\nvs_partition_tool\nvs_tool.py -d minimal device_nvs.bin
```

---

## Screensaver

While idle, the screen runs a particle effect (noof) with a small status overlay showing
WiFi (W) and WireGuard (V) connection dots. The display goes into the screensaver within
a few seconds of no activity. Any touch starts a gesture.

---

## Known hardware quirks

- **LED**: GPIO 4 (blue channel) appears dead on deployed boards. GPIO 17 (labeled red)
  is the only visible LED colour. Both `led_red()` and `led_blue()` calls in the
  firmware use GPIO 17 to ensure visibility.
- **Touch**: XPT2046 IRQ on GPIO 36 (input-only). Tap coordinates are not used — only
  tap timing matters. Calibration code exists in `main/calibration.c` but is not
  compiled.
