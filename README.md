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
pio run -t upload
pio device monitor
```

Expected: the screen alternates MAC (green, one arrow left), WORK (red, one arrow right) and GAME (blue, two arrows left), changing every 3 seconds and the serial
monitor prints `stage 1: display up`.

If something looks wrong:

- Blank screen: check the backlight pin (GPIO45) and the SPI pins.
- Garbage picture: try `ILI9341_2_DRIVER` instead of `ILI9341_DRIVER`.
- Inverted or odd colours: uncomment `TFT_INVERSION_ON` or `TFT_RGB_ORDER` in `platformio.ini`.
- Wrong orientation: change `setRotation()` in `src/main.cpp`.
- Boot loop: the flash settings in `platformio.ini` may not match the board.

## Unverified

Nothing here has been run on the board yet. The pins and display/touch chips come from the
ES3C28P datasheet (LCDWIKI), which appears to be the same design as the Freenove board. The
flash size is confirmed by the chip's Flash ID. Check the pin assignments on first flash.
