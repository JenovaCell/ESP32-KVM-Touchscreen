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
| 3 | Software: BLE keyboard to work laptop / gaming PC, Mac menu-bar app, double-tap Command | built (0.4.x); keyboard pairing verified, Mac link awaiting test |

## Versions

The version lives in the `VERSION` file (x.y.z) and every change is recorded in
`CHANGELOG.md`. The version shows on the device's boot screen and serial output, in the Mac
app's menu (along with the device firmware version it is talking to), and in the release name
(`v0.4.1 (build 14)`). The build number is the CI run number; local builds show `dev`.

To release a change: edit `VERSION`, add a matching entry at the top of `CHANGELOG.md`, and
push. CI fails if the two disagree. Releases use the changelog entry as their notes.

## Reading the diagnostics

**On the board** (small text at the bottom of every screen): the previous event, then the
latest event with `| paired: N`. Each event starts with seconds since boot. Examples:
`paired: Mac app`, `host connected`, `link up, not yet encrypted`, `app subscribed to state`
(the Mac app is really using the link), `refused: unknown device`, `refused: not this target`,
`dropped: remote ended (0x13)` (the other side hung up), `dropped: link timeout (0x08)` (the
radio link was lost), `encryption failed (n)`, `repeat pairing: bond replaced`.

**Key counters** (WORK and GAME screens): `keys rx N tx N fail N` is how many key reports
the board received from the Mac app, passed on to the host, and failed to pass on. The Mac app
menu shows `keys sent N, buffer waits N, dropped N, release re-sends N`. If the Mac says it
sent far more than the board received, reports are being lost between them.

**In the Mac app menu:** "Recent events" lists the latest Bluetooth steps and errors.
"Copy diagnostics" copies everything (versions, status lines, full event list) to the
clipboard; paste it into the Jira ticket. If keyboard access says "waiting" even though
KVMBridge is switched on in Privacy & Security, run
`tccutil reset All io.github.jenovacell.kvmbridge`, reopen the app and grant the prompts
again (see KVM-10).

## Bugs and backlog

Tracked in the KVM Jira project. See `docs/BUG_REPORTS.md` for what to include in a bug and
how fixes flow back to you.

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
