# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
./build.sh metro          # Main KMBox firmware for Adafruit Metro RP2350
./build.sh pico2          # Main KMBox firmware for Pico 2 (RP2350)
./build.sh waveshare      # Main KMBox firmware for Waveshare RP2350-USB-C
./build.sh bridge-metro   # Bridge firmware (Metro RP2350 + ILI9341 TFT)
./build.sh bridge-feather # Bridge firmware (Feather RP2350 + ST7735)
./build.sh dual-metro     # Build both Metro firmwares (KMBox + Bridge)
./build.sh all            # Build everything with interactive flash
./build.sh flash-metros   # Default: build + flash both Metros interactively
```

Append `clean` to force a rebuild, `flash` to flash after building. The default target (no args) is `flash-metros`.

**Prerequisites:** Pico SDK 2.2.0+ (`PICO_SDK_PATH` env var or auto-detected from `~/.pico-sdk/sdk/2.2.0-fresh`), `arm-none-eabi-gcc` 14.2+, CMake 3.13+.

**CI:** GitHub Actions workflows in `.github/workflows/` (build-ci.yml, build-release.yml).

## Architecture

This is **PIOKMbox** — firmware for RP2350 that acts as a transparent USB HID man-in-the-middle. A PC sees your real mouse/keyboard, while serial commands inject synthetic input alongside physical input.

### Dual-Core Design (Single RP2350)

- **Core 0**: TinyUSB **device** stack — presents a mirrored HID device to the PC. Also runs the KMBox serial command parser and the smooth injection engine.
- **Core 1**: TinyUSB **host** stack over PIO-USB — communicates with the physical mouse/keyboard attached to the USB-A port. Reads HID reports and forwards them to Core 0 via shared memory.

The two cores communicate through shared state. Core 1's `flash_safe_execute` support allows Core 0 to safely perform flash operations without race conditions.

### USB Passthrough

When a physical device is connected, the firmware:
1. Caches the device's VID/PID, manufacturer string, and product string
2. On VID/PID change, forces USB re-enumeration so the PC sees the new identity
3. Mirrors all HID report descriptors so the PC sees the same interface layout as the original device

`tusb_config.h` configures 5 HID interfaces (4 mirrored + 1 KMBox control) on the device side and 4 on the host side. The enumeration buffer is sized at 1024 bytes for large gaming device descriptors.

### Serial Command Injection (KMBox Protocol)

Commands can arrive via three paths:
1. **UART** from a second RP2350 board (the Bridge) or USB-UART adapter
2. **USB HID control interface** — a dedicated vendor output report (Report ID 0xF0) on the 5th HID interface. This allows sending KM commands directly over the same USB cable as HID passthrough, with no UART or bridge required. Device control commands (DPI, RGB, etc.) are still forwarded transparently to the physical mouse via non-KMBox report IDs.
3. **`kmbox_process_command_buffer()`** processes both text and 8-byte binary packets received via either path

The parser (`kmbox_serial_handler.c`) supports:
- **Text commands** (`km.move(x,y)`, `km.click(n)`, `km.left(1)`, etc.)
- **8-byte binary protocol** (< 50 µs latency, 1000+ commands/sec)
- **Monitor mode** for real-time button state queries
- Compatible with KMBox B+, Ferrum, and Macku tool protocols

#### USB HID Control Interface (KMBox on USB)

The 5th HID interface (always present, instance index = `kmbox_control_instance()`) provides:
- **Output report (64 bytes, no report ID)**: PC sends KM commands. Bytes 0-1 are the `0xF0 0xAA` magic prefix, bytes 2-63 the KM command payload. Delivered to `tud_hid_set_report_cb` via **either** SET_REPORT (WebHID `sendReport`) **or** the interrupt OUT endpoint (hidapi `dev.write`) — the interface exposes both an IN and an OUT endpoint.
- **Feature report (64 bytes)**: PC queries status (humanization mode, queue depth)
- All non-KMBox SET_REPORT traffic on mirrored interfaces is forwarded to the physical device transparently (DPI, RGB, macros, etc.)

**Important:** the KMBox control interface is the *last* HID interface, so `dev.open(vid, pid)` in hidapi opens the first (mouse) interface, not the control interface. You must enumerate and open the control interface by path. The first byte hidapi sends is the report ID — use `0` since the control interface has no report ID. The firmware accepts the `0xF0 0xAA` magic prefix at byte 0 (WebHID sends the report verbatim) *or* at byte 1 behind a leading `0x00` report-ID byte (hidapi contract).

PC-side usage with hidapi:
```python
import hid

# Enumerate the proxied device; the KMBox control interface is the LAST one.
devices = hid.enumerate(0xXXXX, 0xXXXX)  # VID/PID of the proxied device
# The enumerate() list is ordered by interface number — pick the highest.
control = max(devices, key=lambda d: d['interface_number'])
dev = hid.device()
dev.open_path(control['path'])

# Send a KM text command: byte 0 = report ID 0, bytes 1-2 = 0xF0 0xAA magic
dev.write(b'\x00' + b'\xf0\xaa' + b'km.move(100,50)\x00' + b'\x00'*45)  # 64-byte report
# Send an 8-byte fast binary command
dev.write(b'\x00' + b'\xf0\xaa' + fast_move_packet + b'\x00'*53)  # padded to 64 bytes
```

### Movement Humanization

`smooth_injection.c` provides sub-pixel precision (16.16 fixed-point) movement injection with:
- Velocity-matched spreading across HID frames
- Configurable max per-frame rate limiting
- Movement queue (64 entries) for async injection

`humanization_lut.c` and `humanization_fpu.c` add jitter, overshoot simulation, Bezier easing, and per-session randomization. Four modes (OFF/LOW/MEDIUM/HIGH) controlled via GPIO 7 button or serial command. See `HUMANIZATION.md` for full details.

### Two-Board Hardware Topology

```
Mouse/Keyboard → [Board 1: Main KMBox] → USB-C → PC
                      ↕ UART (crossed)
                 [Board 2: Bridge] → ILI9341 TFT
                      ↕ USB CDC
                 [PC Tool / Script]
```

- **Board 1** runs `PIOKMbox.c` — the USB proxy and command executor
- **Board 2** runs `bridge/main.c` — receives commands from PC over USB CDC, relays them to Board 1 over UART, and drives the ILI9341 display

### Bridge Firmware (`bridge/`)

Separate CMake project with its own `CMakeLists.txt`. Key modules:
- `main.c` / `main_feather.c` — entry points (Metro vs Feather boards)
- `ili9341.c` / `st7735.c` — display drivers (ILI9341 via SPI, ST7735)
- `tft_display.c` — rendering: latency graphs, status, thermal gauges
- `hw_uart.c` — bidirectional UART relay to Board 1
- `latency_tracker.c` — performance monitoring
- `core1_translator.c` — color tracking / CV on Core 1
- `bridge_client.py` — Python test/control client
- `ferrum_translator.c`, `makcu_translator.c` — alternative protocol support

### Key Source Files (Main Firmware)

| File | Role |
|------|------|
| `PIOKMbox.c` | Entry point, dual-core init, main loop, button handling |
| `usb_hid.c` (140KB) | USB HID device + host implementation, descriptor mirroring, report forwarding |
| `kmbox_serial_handler.c` (50KB) | KMBox text/binary command parsing and dispatch |
| `smooth_injection.c` (51KB) | Sub-pixel movement injection engine |
| `humanization_lut.c` (21KB) | Precomputed jitter/Bezier lookup tables |
| `humanization_fpu.c` | FPU-accelerated humanization math |
| `led_control.c` (33KB) | NeoPixel RGB + status LED control |
| `watchdog.c` | Hardware + software watchdog for USB stack recovery |
| `state_management.c` | System state struct (timing, flags, button debounce) |
| `xbox_host.c` / `xbox_device.c` | Xbox GIP controller passthrough |
| `defines.h` | All `#define` constants: pins, timing, modes, build configs |
| `config.h` | Build-config-dependent overrides (retry counts, delays) |
| `timing_config.h` | Frame timing constants |
| `tusb_config.h` | TinyUSB stack configuration (endpoints, buffers, dual role) |
| `ws2812.pio` | PIO assembly for NeoPixel bit-banging |
| `dcp_helpers.S` | RP2350 DCP (crypto) hardware acceleration helpers |

### Library Structure (`lib/`)

All are git submodules with their own CMakeLists.txt:

| Library | Purpose |
|---------|---------|
| `Pico-PIO-USB` | Bit-banged USB host/device via PIO |
| `kmbox-commands` | KMBox command protocol definitions |
| `fast-protocol` | 8-byte binary protocol constants |
| `wire-protocol` | Wire-format serialization utilities |
| `dma-uart` | DMA-accelerated UART transfers |
| `hid-defs` | HID usage page/ID constants |
| `peri-clock` | Peripheral clock configuration helpers |
| `led-utils` | LED abstraction utilities |

### Build Configuration Presets

Set via `BUILD_CONFIG` compile definition in CMake (default: `BUILD_CONFIG_DEVELOPMENT`):
- `BUILD_CONFIG_DEVELOPMENT` — verbose logging
- `BUILD_CONFIG_PRODUCTION` — optimized, higher retry counts, longer stabilization delays
- `BUILD_CONFIG_TESTING` — minimal delays, fast iteration
- `BUILD_CONFIG_DEBUG` — maximum retries, full debug symbols

### Supported Boards

Pin assignments are set via CMake compile definitions based on `PICO_BOARD` (see `CMakeLists.txt`):

| Board | MCU | USB Host D+/D- | 5V Power | LED | WS2812 | Button |
|-------|-----|---------------|----------|-----|--------|--------|
| **Metro RP2350** (`adafruit_metro_rp2350`) | RP2350B | GPIO 32/33 | GPIO 29 | GPIO 23 | GPIO 25 | GPIO 7 |
| **Pico/Pico2** (`pico2`) | RP2350 | GPIO 16/17 | GPIO 18 | GPIO 25 | GPIO 21 | GPIO 7 |
| **Waveshare RP2350-USB-C** (`waveshare_rp2350_usb_c`) | RP2350A | GPIO 12/13 | N/A (Type-C) | N/A | GPIO 16 | N/A |

**Waveshare RP2350-USB-C notes:**
- Uses RP2350A (30 GPIOs, 15 exposed on header)
- Two USB-C ports: one native (for PC connection), one PIO-USB (for mouse/keyboard)
- No separate USB 5V power enable pin — Type-C handles power negotiation
- No discrete LED — all status indication via WS2812 RGB LED
- No user button — humanization mode changes via serial commands only
- 2 MB onboard flash (W25Q16JVUXIQ)

UART instance is selectable at build time via `-DKMBOX_UART_INSTANCE=0` (UART0 on GPIO 0/1) or `=1` (UART1 on GPIO 8/9).

### Clock Speeds

- Main KMBox: 300 MHz (set in CMake, `defines.h` defaults to 240 MHz)
- Bridge: 280 MHz (balanced for display + UART)
- Optimizations: `-O3 -ffast-math -funroll-loops -ffunction-sections -fdata-sections` with `--gc-sections`

## Development Notes

- Always run `git submodule update --init --recursive` before building (the build script auto-checks for `lib/Pico-PIO-USB/CMakeLists.txt`)
- UART wiring between boards must be crossed (TX→RX, RX→TX) with shared GND
- Debug output is available on the KMBox UART (GPIO 0/1 on Metro at 115200 baud)
- USB CDC is disabled on the main firmware to avoid breaking HID enumeration; debug output goes through the UART/bridge interface
- Flash via BOOTSEL mode (hold BOOTSEL while connecting USB) then copy `.uf2` to the mounted RPI-RP2 drive, or use `picotool`
- Pin value `255` means "not available" — source code uses `#if PIN_XXX != 255` guards to skip GPIO init for absent pins
- When adding a new board: create a header in `boards/`, add pin config to `CMakeLists.txt` board detection block, add build target to `build.sh`, and verify all pin guards in source files
- The project requires git-lfs for some assets
