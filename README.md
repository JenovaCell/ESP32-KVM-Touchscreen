# KVM indicator (ESP32-S3 touch display)

A small desk device that shows which machine your keyboard is driving (MacBook or work laptop).
The keyboard stays on the MacBook. A Mac app captures keystrokes and, in "Work" mode, forwards
them over Bluetooth LE to the ESP32, which presents itself to the work laptop as a BLE keyboard.
Nothing is installed on the work laptop.

Target board: Freenove ESP32-S3 CYD 2.8" 240x320 capacitive touch.

## Stages

| Stage | Scope | Status |
|-------|-------|--------|
| 1 | Display: MAC / WORK / GAME indicator | working on the board |
| 2 | Controls: touch to switch, saved state | built, awaiting test on the board |
| 3 | Software: BLE HID to work laptop, Mac menu-bar app, double-tap Command | not started |

## Flash and check

Easiest: run the **build** workflow (Actions tab, Run workflow), then download
`kvm-merged.bin` from the newest release and flash it at `0x0` in esptool.js.
Or locally:

```
cd firmware
pio run -t upload
pio device monitor
```

Expected (stage 2): the screen shows MAC (green, one arrow left) on first boot. Tap the left
half to move toward GAME (blue, two arrows left), the right half to move toward WORK (red, one
arrow right). The order is GAME, MAC, WORK and it stops at the ends. The last target is
remembered across power cycles. The serial monitor prints each tap's raw coordinates.

If something looks wrong:

- Blank screen: check the backlight pin (GPIO45) and the SPI pins.
- Garbage picture: try `ILI9341_2_DRIVER` instead of `ILI9341_DRIVER`.
- Inverted colours: `KVM_INVERT_DISPLAY` in `platformio.ini` (on by default for this panel).
- Red and blue swapped: try `TFT_RGB_ORDER=TFT_BGR`.
- Wrong orientation: change `setRotation()` in `src/main.cpp`.
- Taps move the wrong way: set `KVM_TOUCH_FLIP_X=1` in `platformio.ini`.
- Boot loop: the flash settings in `platformio.ini` may not match the board.

## Verified on the board

Stage 1 (display, pins, inversion) is confirmed from photos. Touch (stage 2) has not been
tested yet. The pins come from the ES3C28P datasheet (LCDWIKI), the same design as the
Freenove board. Flash size is confirmed by the chip's Flash ID.
