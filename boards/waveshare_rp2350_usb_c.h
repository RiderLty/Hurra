/*
 * Board header for Waveshare RP2350-USB-C
 *
 * Key specs:
 *   - RP2350A (QFN-60, 30 GPIO) — NOT RP2350B
 *   - 2 MB QSPI flash (W25Q16JVUXIQ)
 *   - PIO USB Host on Type-C connector: D+ = GPIO12, D- = GPIO13
 *   - No separate USB 5V power enable pin (Type-C handles power)
 *   - WS2812 RGB LED on GPIO16
 *   - No discrete LED
 *   - 15 GPIOs broken out (GPIO0-10, GPIO26-29)
 *   - UART0 TX=GPIO0, RX=GPIO1
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_WAVESHARE_RP2350_USB_C_H
#define _BOARDS_WAVESHARE_RP2350_USB_C_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// For board detection
#define WAVESHARE_RP2350_USB_C

// --- RP2350 VARIANT ---
// RP2350-USB-C uses the RP2350A (QFN-60, 30 GPIO).
#define PICO_RP2350A 1

// --- BOARD-SPECIFIC PINS ---

// PIO USB Host on Type-C connector
#define WAVESHARE_RP2350_USB_C_USB_HOST_DP_PIN   12
#define WAVESHARE_RP2350_USB_C_USB_HOST_DM_PIN   13

// WS2812 RGB LED
#define WAVESHARE_RP2350_USB_C_WS2812_PIN        16

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- LED ---
// No discrete LED on this board; set to 255.
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 255
#endif

// --- RGB (WS2812) LED ---
#ifndef PICO_DEFAULT_WS2812_PIN
#define PICO_DEFAULT_WS2812_PIN 16
#endif

// --- PIO USB ---
#define PICO_DEFAULT_PIO_USB_DP_PIN WAVESHARE_RP2350_USB_C_USB_HOST_DP_PIN

// --- FLASH ---
// Winbond W25Q16JVUXIQ (2 MB)
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

// --- A2 SILICON SUPPORT ---
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
