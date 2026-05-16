# OpenWolf

@.wolf/OPENWOLF.md

This project uses OpenWolf for context management. Read and follow .wolf/OPENWOLF.md every session. Check .wolf/cerebrum.md before generating code. Check .wolf/anatomy.md before reading files.


@../.wolf/OPENWOLF.md

# gmabutton

ESP32-2432S028 ("CYD") synchronized alert button. Pair of devices. Tap = blue
"call me", double-tap or long-press = red "help me", two-tap dismiss. First
boot or 10 s screen hold = SoftAP setup wizard at `192.168.4.1`. Optional
WireGuard client tunnel to a remote server.

## Build, Flash, and Monitor

**Read `FLASH.md` for the full recipe.** It is the only correct way to
build/flash this project on Windows. Do not improvise — the ESP-IDF
toolchain has Windows-specific quirks (MSYSTEM, IDF_PYTHON_ENV_PATH) that
will silently break commands run without the boilerplate.

Always ask the user which **COM port** the target CYD is on before flashing.
Never assume a port number.

## Hardware reference

`../klipperbridge/espcyd.md` — full CYD pinout, ILI9341 driver patterns,
ESP-IDF toolchain install steps. The pinout used here (display SPI, touch
IRQ, backlight) matches that document. The RGB LED on the back (GPIO 4/16/17,
active-LOW) is gmabutton-specific and documented in `main/led.c`.
