# Flashing gmabutton onto a CYD

This is the only correct way to build, flash, and monitor this project on
Windows. Copy each PowerShell block verbatim and run it via the `PowerShell`
tool. **Always ask the user for the COM port first.** Do not assume.

---

## Step 1 — Find the COM port

```powershell
Get-WmiObject Win32_SerialPort | Select-Object Name, DeviceID, Description
```

Look for **CP210x** or **CH340** in the Description column. The DeviceID is
the port (e.g. `COM5`). **Ignore "Standard Serial over Bluetooth link"** —
that's not the CYD.

If multiple CYD-style ports appear and the user is flashing two devices,
ask them which physical device is plugged in.

---

## Step 2 — Build

```powershell
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
$Env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.13_env'
$Env:IDF_TOOLS_PATH = 'C:\Espressif'
Set-Location 'C:\Espressif\frameworks\esp-idf-v5.5.3'
. .\export.ps1 | Out-Null
Set-Location 'C:\Users\Troutstar\Desktop\Ideas\gmabutton'
idf.py build
```

All four env/Set-Location lines are mandatory:
- `Remove-Item Env:MSYSTEM` — Claude Code runs inside MSYS2; idf.py refuses
  to run when MSYSTEM is set. This silently breaks otherwise.
- `IDF_PYTHON_ENV_PATH` — pins the right Python venv.
- `IDF_TOOLS_PATH` — tells idf.py where Xtensa GCC etc. live.
- `. .\export.ps1` — populates PATH, IDF_PATH.

If `idf.py` is "not found" or fails with a Python error, the above lines
were missed. Re-run the whole block.

---

## Step 3 — Flash

Substitute the user's COM port for `<PORT>`. Same env preamble as step 2:

```powershell
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
$Env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.13_env'
$Env:IDF_TOOLS_PATH = 'C:\Espressif'
Set-Location 'C:\Espressif\frameworks\esp-idf-v5.5.3'
. .\export.ps1 | Out-Null
Set-Location 'C:\Users\Troutstar\Desktop\Ideas\gmabutton'
idf.py -p <PORT> flash
```

Flash takes ~30 s. The device auto-resets when done.

---

## Step 4 — Monitor (optional, diagnostics only)

```powershell
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
$Env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.13_env'
$Env:IDF_TOOLS_PATH = 'C:\Espressif'
Set-Location 'C:\Espressif\frameworks\esp-idf-v5.5.3'
. .\export.ps1 | Out-Null
Set-Location 'C:\Users\Troutstar\Desktop\Ideas\gmabutton'
idf.py -p <PORT> monitor
```

Exit with `Ctrl+]`. **Do not leave monitor running** — it holds the COM
port open and will block the next flash.

You can also combine flash + monitor in one call: `idf.py -p <PORT> flash monitor`.

---

## First-boot behaviour (what the user will see after flashing)

1. Screen shows **SETUP MODE** with an AP SSID: `gmabutton-XXXX` (last 4 hex
   digits of the device MAC).
2. User connects a phone/laptop to that open SoftAP.
3. User browses to `http://192.168.4.1/` — single-page setup form.
4. They fill in: WiFi SSID + password, peer IP (the *other* CYD's address),
   optional WireGuard client credentials.
5. Submit → device saves to NVS and reboots into normal mode.

The peer IP is whatever address the other device will get on the chosen
network (or the WireGuard tunnel IP, if that's configured on both).

---

## Factory reset

Hold a finger on the screen for **10 seconds**. The screen flashes white,
NVS is wiped, and the device reboots into Setup Mode. This is implemented
as the `GESTURE_LONG_HOLD` event in `main/touch.c` and handled in
`handle_gesture()` in `main.c`.

---

## Common failure modes

| Symptom | Cause | Fix |
|---------|-------|-----|
| `'idf.py' is not recognized` | Env preamble missing | Re-run the full block from step 2 |
| Flash fails with "could not open port" | Monitor still attached | Close the previous monitor (`Ctrl+]`) and retry |
| `MSYSTEM=MINGW64` warning | Forgot `Remove-Item Env:MSYSTEM` | Add it as the first line and retry |
| Brick / no boot after flash | Likely a bad write; retry. If repeated, hold BOOT and press RESET on the device while running flash (user must do this physically) | Ask the user to hold BOOT |
| Display garbled colours | Wrong MADCTL — not gmabutton's fault if `ili9341.c` is unmodified | Check `klipperbridge/espcyd.md` MADCTL section |
| Touch unresponsive | XPT2046 IRQ on GPIO 36 only — coordinates not read, just tap timing | This is intentional. Tap anywhere counts. |

---

## Notes for two-device deployments

Flash **one device at a time**. Do not flash with both plugged in unless
you know which COM port is which. After flashing both:

1. Device A first-boot → setup wizard → enter WiFi + device B's eventual IP
2. Reboot A — it joins WiFi and gets its actual IP. Note it.
3. Device B first-boot → setup wizard → WiFi + device A's IP (from step 2)
4. Open `http://<A-ip>/config` and update A's peer_ip to B's actual IP.

If both devices are on a WireGuard tunnel, use the tunnel IPs (e.g.
`10.0.0.2` / `10.0.0.3`) instead — those stay stable across networks.

---

## App partition note

The default ESP-IDF partition layout gives a 1 MB app slot, and the current
build uses ~951 KB (7% free). The CYD has 4 MB of flash, so this can be
expanded to a single ~3.5 MB factory slot or two ~1.5 MB OTA slots by
adding a `partitions.csv` and setting `CONFIG_PARTITION_TABLE_CUSTOM=y` in
`sdkconfig.defaults`. Not done yet — revisit if the firmware grows past
~900 KB or if OTA is required. Details in `.wolf/cerebrum.md`.
