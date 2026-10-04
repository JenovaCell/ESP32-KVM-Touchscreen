# KVM indicator (ESP32-S3 touch display)

A small desk device that shows which machine your keyboard is driving (MacBook or work laptop).
The keyboard stays on the MacBook. A Mac app captures keystrokes and, in "Work" mode, forwards
them over Bluetooth LE to the ESP32, which presents itself to the work laptop as a BLE keyboard.
Nothing is installed on the work laptop.

Target board: Freenove ESP32-S3 CYD 2.8" 240x320 capacitive touch.

## Stages

| Stage | Scope | Status |
|-------|-------|--------|
| 1 | Display: MAC / WORK indicator | scaffolded, **not compiled or flashed** |
| 2 | Controls: touch toggle, saved state, output mode | not started |
| 3 | Software: BLE HID to work laptop, Mac menu-bar app, double-tap Command | not started |

## Stage 1: flash and check

```
cd firmware
# 1. Fill in the BOARD VALUES block in platformio.ini (pins, driver, flash).
pio run -t upload
pio device monitor
```

Expected: the screen alternates MAC (green, arrow left) and WORK (red, arrow right) every 3 seconds and the serial
monitor prints `stage 1: display up`.

If something looks wrong:

- Blank screen: check the backlight pin and the SPI pins.
- Inverted or odd colours: uncomment `TFT_INVERSION_ON` or `TFT_RGB_ORDER` in `platformio.ini`.
- Wrong orientation: change `setRotation()` in `src/main.cpp`.
- Boot loop: the flash settings in `platformio.ini` may not match the board.

## Unverified

Nothing here has been compiled or run. Every value in the BOARD VALUES block of
`firmware/platformio.ini` is a placeholder until checked against the Freenove tutorial.
The build fails on purpose until the pins are set.
