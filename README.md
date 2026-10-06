# KVM indicator (ESP32-S3 touch display)

A small desk device that shows which machine your keyboard is driving (MacBook, work laptop or
gaming PC). The keyboard stays on the MacBook. A Mac app captures keystrokes and, in Work or
Game mode, sends them over the **USB cable** to the ESP32, which presents itself over Bluetooth
LE as a keyboard to the work laptop or gaming PC. Nothing is installed on those machines.

```
MacBook keyboard -> Mac app --USB cable--> board --Bluetooth--> work laptop / gaming PC
```

The board must be plugged into the Mac to use the Mac keyboard. Before 0.5.0 the Mac app used
Bluetooth for this link too; it could not keep up with typing (see KVM-16, KVM-17).

Target board: Freenove ESP32-S3 CYD 2.8" 240x320 capacitive touch.

## Stages

| Stage | Scope | Status |
|-------|-------|--------|
| 1 | Display: MAC / WORK / GAME indicator | working on the board |
| 2 | Controls: touch to switch, saved state | built, awaiting test on the board |
| 3 | Software: Bluetooth keyboard to work laptop / gaming PC, Mac menu-bar app, double-tap Command | built; Bluetooth keyboard pairing verified; Mac link moved to USB in 0.5.0, awaiting test |

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
`paired: work host`, `work host connected`, `link up, not yet encrypted`,
`refused: unknown device`, `refused: pair on WORK/GAME screen`,
`dropped: remote ended (0x13)` (the other side hung up), `dropped: link timeout (0x08)` (the
radio link was lost), `encryption failed (n)`, `repeat pairing: bond replaced`.

**Key counters** (WORK and GAME screens): `keys rx N tx N fail N` is how many key reports
the board received from the Mac app over USB, passed on to the host, and failed to pass on.
The Mac app menu shows `keys sent N, release re-sends N, write errors N, board lines N`. If the
Mac says it sent far more than the board received, reports are being lost on the cable.

**In the Mac app menu:** "Board link" says whether the Mac found the board on a USB port.
"Recent events" lists the latest link steps and errors.
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
`kvm-merged-v<version>.bin` (or the plain `kvm-merged.bin`, same file) from the newest release
and flash it at `0x0` in esptool.js.
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

## Using it

1. Plug the board into the Mac with the USB-C cable (this also powers it). Flash it if needed.
2. Open KVMBridge. Its menu should say "Board link: Connected (USB)" and show the board's
   firmware version. If it says it cannot find the board, check the cable (some cables only
   charge).
3. Pair the work laptop with "Desk Keyboard" while the board is on the WORK screen (type the
   code from the board's screen). Pair the gaming PC the same way on the GAME screen.
4. (Optional) Switch "Auto-switch by app" on in the Mac menu: Elgato Studio in front selects WORK,
   Moonlight selects GAME, any other app selects MAC. It acts only when the frontmost app
   changes, so a manual switch stays until you change app.
5. On WORK or GAME the Mac's volume, mute, play/pause, track and brightness keys control that PC.
   (After flashing 0.8.0, re-pair the PC once; see the CHANGELOG.)
6. When the Mac sleeps, its display sleeps or you lock it, the board's screen goes dark and comes
   back when the Mac wakes, when you type, or when you touch the screen.
7. Switch targets by tapping the left or right half of the board's screen, or by double-tapping
   Left Command (toward GAME) or Right Command (toward WORK) on the Mac. The work laptop and
   gaming PC stay connected to the board on every screen, so switching does not wait for a
   reconnect; only the PC of the current target receives keys. New devices can only be paired
   on the WORK or GAME screen.

Upgrading to 0.5.0: replace both the firmware and the Mac app. The board forgets the Mac's
old Bluetooth pairing on first boot and keeps the work laptop and gaming PC pairings. If the
Mac menu says "Keyboard access: waiting" although KVMBridge is switched on, run the
`tccutil reset All ...` command above.

Pairings and firmware updates: the board never wipes pairings by itself. To update, flash
`firmware-v<version>.bin` at `0x10000` (app only, no erase): the work laptop pairing survived
this when tested (0.5.4 flashed over 0.5.4). The all-in-one `kvm-merged-v<version>.bin` at `0x0`
is for a first flash; whether it keeps pairings has not been tested. Once, after moving from
0.5.2 to 0.5.4, the work laptop had to be paired again (KVM-22); that pairing predates the
tested flash and the cause was not identified. If a device stops typing after an update, hold the
screen for 4 seconds on that target's screen (forget host), remove "Desk Keyboard" on the device,
and pair again.

## Verified on the board

The display, pins, inversion, touch, saved target, and Bluetooth keyboard pairing with the work
laptop are confirmed on the hardware. Relaying keystrokes to the work laptop works but was
unusable over the old Bluetooth Mac link (KVM-16). The USB link (0.5.0) is not yet tested on the
hardware. The pins come from the ES3C28P datasheet (LCDWIKI), the same design as the Freenove
board. Flash size is confirmed by the chip's Flash ID.
