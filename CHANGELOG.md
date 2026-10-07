# Changelog

All notable changes to this project are recorded here. The version number lives in the
`VERSION` file and is shown on the device's boot screen, in the Mac app's menu and in release
names. Format follows [Keep a Changelog](https://keepachangelog.com/); versions follow
[Semantic Versioning](https://semver.org/) (while the major number is 0, a minor bump may
change behaviour).

"Verified" means checked on the real hardware. "Unverified" means it compiles in CI but has not
been tried on the board yet.

## [0.10.0] - 2026-10-07

Mouse sharing: the pointer follows the target together with the keyboard (KVM-9).

### Added
- Board and Mac app (KVM-9): while WORK or GAME is the target, the Mac's pointer is frozen and its
  movement, clicks (left, right, middle) and scrolling (vertical and horizontal) go to that PC as a
  Bluetooth mouse. On MAC the pointer works normally. Keyboard and pointer always move together.
- Reclaim rules: any desktop or app change on the Mac (three-finger swipe, Mission Control, Cmd+Tab)
  gives keyboard and pointer back to the Mac, or, with "Auto-switch by app" on, re-checks the front app
  (Elgato Studio gives WORK, Moonlight gives GAME, anything else gives MAC). Double-tap Command, the
  board's touch screen and a lost board link give the pointer back to the Mac as well, and quitting
  the app always unfreezes it.
- Mac menu: "Pointer speed (Work/Game)" (0.5x to 2x) and "Invert scroll direction (Work/Game)", both
  remembered.
- Board: new Mac app command `@M <buttons> <dx> <dy> <wheel> <pan>`. Movement is summed on the board
  and sent at most every few milliseconds; a failed send is retried, so no movement is lost.

### Upgrade notes
- **Re-pair the work laptop and the gaming PC once after flashing 0.10.0.** The keyboard's description
  changed again (it now also has a mouse) and Windows keeps the old description until the device is
  removed and paired again: remove "Desk Keyboard" in Windows Bluetooth settings, hold the screen for 4
  seconds on that target's screen (forget host), then pair again.
- Flash the firmware and replace the Mac app together.

### Status
- Unverified on hardware. Known limits: the Mac pointer is frozen but may stay visible (macOS only hides it for
  the front app); pointer acceleration is applied on the Mac and again on Windows, so adjust the speed.

## [0.9.0] - 2026-10-06

Screen sleep when the Mac is asleep, locked or off (KVM-26).

### Added
- Mac app and board (KVM-26): when the Mac goes to sleep, its display sleeps or the screen is locked,
  the app tells the board and the board turns its backlight off. When the Mac wakes or unlocks, the
  backlight comes back. Any key typed through the Mac, or the Mac app starting again, also wakes it.
  Touching the screen wakes it for 30 seconds (that touch is not treated as a tap, so it cannot switch
  target by accident). If the Mac app is silent for 60 seconds (Mac shut down, cable unplugged, app
  quit) the screen goes off too.
- The Bluetooth links to the work laptop and gaming PC stay up while the screen is off, so switching
  is as fast as before. Only the backlight is switched off; the board does not deep-sleep.
- Board: new Mac app command `@Z <1|0>`. **Flash the firmware and replace the Mac app together.**

### Status
- Unverified on hardware.

## [0.8.0] - 2026-10-06

Media keys on WORK and GAME (KVM-6).

### Added
- Mac app and board (KVM-6): while WORK or GAME is the target, the Mac's volume up/down, mute,
  play/pause, next/previous track (and fast-forward/rewind) and brightness up/down keys are sent to
  that PC as media keys instead of acting on the Mac. Function keys without Fn are these media
  keys on a Mac keyboard; with Fn they are F1-F12 as before. Keys the Mac handles itself
  (keyboard backlight, Mission Control, Launchpad, Spotlight, dictation) stay on the Mac. Media
  keys held down when you switch target are released first.
- Board: a second HID report (consumer control, report ID 2) and a new Mac app command `@C <4 hex>`.

### Upgrade notes
- **Re-pair the work laptop and the gaming PC once after flashing 0.8.0.** The keyboard's description
  changed (it now has media keys) and Windows keeps the old description until the device is removed
  and paired again: remove "Desk Keyboard" in Windows Bluetooth settings, hold the screen for 4 seconds
  on that target's screen (forget host), then pair again.
- Flash the firmware and replace the Mac app together.

### Status
- Unverified on hardware.

## [0.7.0] - 2026-10-06

Auto-switch the target from the frontmost Mac app (KVM-24).

### Added
- Mac app (KVM-24): menu item "Auto-switch by app". When on, the frontmost application picks the
  target: an app named "Elgato Studio" gives WORK, "Moonlight" gives GAME, every other app gives
  MAC. It only acts when the frontmost app changes, so a manual switch (touch or double-tap
  Command) stays until you move to another app. Off until you switch it on; the choice is
  remembered. Apps are matched by name (case ignored); the names are at the top of
  `AutoSwitch.swift`.
- Board (KVM-24): new Mac app command `@G <0|1|2>` goes straight to a target. **Flash the firmware
  and replace the Mac app together.** Older firmware ignores the command (the menu item then does
  nothing).

### Status
- Unverified on hardware. Open check: the exact app name of "Elgato Studio" on this Mac.

## [0.6.0] - 2026-10-06

Faster switching (KVM-23, option A): both PCs stay connected, the target only chooses who gets keys.

### Changed
- Board (KVM-23): the work laptop and gaming PC stay connected to the board on every screen,
  including MAC. The board advertises whenever it has a free link instead of only on WORK and
  GAME. Switching target therefore no longer waits for the PC to scan and reconnect (up to 10 s
  on MAC to WORK before). Key reports are sent only to the current target's PC, never to both.
- Board: when you leave WORK or GAME, an "all keys up" report is sent to that PC first, so no key
  stays pressed there.
- Board: an unknown device can only be paired on the WORK or GAME screen (`refused: pair on
  WORK/GAME screen` on the MAC screen). Events now read `work host connected` / `game host
  connected`.

### Removed
- The "refused: MAC screen" and "refused: not this target" refusals: known PCs are accepted on any
  screen.

### Status
- Unverified on hardware. Expected: after a switch the PC types within about a second; the
  Windows Bluetooth list shows "Desk Keyboard" as connected even while the Mac is active.
- Risk to check: keys must reach only the current target's PC, and Game was not tested before.

## [0.5.4] - 2026-10-06

Decision KVM-15 (option C): keep the identity-address matching from 0.4.2, remove the pairing wipe.

### Changed
- Board (KVM-15): the automatic pairing wipe on a schema change is gone, so the firmware no
  longer wipes pairings by itself. In testing the work laptop still had to be paired once after
  an app-only flash of this version; the cause is under investigation (KVM-22).
- README: documents what happens to pairings on upgrade and how to pair a device again.

### Status
- Unverified on hardware. Check that, after flashing, the Work PC still types without pairing again.

## [0.5.3] - 2026-10-06

Fix for KVM-20 (a held key stops repeating on WORK) and KVM-21 (Ctrl+C, Ctrl+Shift+S fail on WORK).

### Fixed
- Mac app: once a second the board repeats its current target, and the app treated every
  repeat as a target change and released all keys on the other computer. That sent an
  "all keys up" report to the Work PC every second, even while a key or Ctrl was held, which
  cut off held keys and dropped the Control part of shortcuts. The app now releases keys only
  when the target or the link state actually changes. Introduced in 0.5.0 (found with the
  0.5.2 key trace).

### Status
- Unverified on hardware. Mac app change only; the firmware is unchanged from 0.5.2.

## [0.5.2] - 2026-10-06

Diagnostic build for KVM-20 (holding a key on WORK repeats inconsistently). **No change to
typing behaviour.** It only records what happens during a key hold.

### Added
- Mac app (KVM-20): "Copy diagnostics" now ends with a millisecond-stamped key trace: each key
  down (marked auto-repeat when macOS repeats it), key up, each report sent to the board, each
  "all keys up" re-send, and a `board` line for every report the board relayed to the PC (its own
  clock in ms, `+` sent / `-` failed).
- Board (KVM-20): for each key report it relays it sends an `@R` line back to the Mac app.

### Status
- Unverified on hardware. Does not fix KVM-20; it is meant to show its cause.

## [0.5.1] - 2026-10-06

Release packaging only (KVM-19). No change to the firmware or the Mac app behaviour.

### Changed
- Release files now carry the version in their name: `kvm-merged-v0.5.1.bin`,
  `firmware-v0.5.1.bin`, `KVMBridge-v0.5.1.zip`. The plain-named copies (`kvm-merged.bin`,
  `firmware.bin`, `KVMBridge.zip`) are still attached, so `releases/latest/download/...` links
  keep working.

## [0.5.0] - 2026-10-06

**Breaking: the Mac app now talks to the board over the USB cable instead of Bluetooth.**
Decision in KVM-17, implementation in KVM-18. This is the fix path for KVM-16 (letters repeat
on the Work PC; the old Bluetooth link filled up and timed out) and removes the Mac pairing
behind KVM-13. Replace the firmware and the Mac app together.

### Changed
- Mac app and board talk over a serial port on the USB-C cable. Simple text lines: `@H`
  heartbeat once a second, `@K <hex>` key report, `@S L|R` step target; the board answers
  with `@T <target>` and `@V <version>`. Lines without `@` (debug text) are ignored.
- The app finds the board by itself and rescans every second, so unplugging and replugging the
  cable recovers without restarting anything. The link counts as lost after 3 s of silence.
- Bluetooth is now used only for the work laptop and the gaming PC. On the MAC screen the board
  does not advertise and disconnects any host.
- Mac app: no Bluetooth permission is needed any more. The menu shows "Board link" instead of
  "Bluetooth" and counters `keys sent, release re-sends, write errors, board lines`.
- Board: the main loop runs every 3 ms (was 15 ms) so keystrokes are relayed quickly.
- Kept from 0.4.4: the app only sends a key report when the key state changes, and re-sends
  "all keys up" shortly after the last release.

### Removed
- The Bluetooth control service, the Mac app's Bluetooth pairing and its role on the board.
  The Mac's old pairing is removed from the board on first boot; the work laptop and gaming PC
  pairings are kept.
- The Bluetooth write queue and flow control in the Mac app (not needed on a wired link).

### Notes
- The board's normal debug text (`Serial`) stays on its UART pins. The USB link uses the chip's
  built-in USB Serial/JTAG port directly, so debug text never mixes with Mac messages.

### Upgrade notes
- The board must be plugged into the Mac to use the Mac keyboard.
- Flash `kvm-merged.bin`, replace the Mac app, and check the menu says "Connected (USB)".
- If the menu says "Keyboard access: waiting" although KVMBridge is switched on, run
  `tccutil reset All io.github.jenovacell.kvmbridge`, reopen the app and grant the prompts.

### Status
- Verified on hardware (macOS 27): USB link, target switching with double-tap Right Command,
  unplug/replug recovery, no more repeating letters on WORK. Known issue: holding a key repeats
  inconsistently on WORK (KVM-20).

## [0.4.4] - 2026-10-06

Fix attempt and instrumentation for KVM-16 (letters repeat on the Work PC). The cause is not
yet proven: the safeguards below may hide the symptom, so counters ship with them to show
where a key release goes missing.

### Added
- Board (KVM-16): the WORK and GAME screens show `keys rx N tx N fail N`: key reports received
  from the Mac app, notifications sent to the host, and failures where the library reports
  them. Compare `rx` with what you typed to see whether keys reach the board.
- Mac app (KVM-16): the menu shows `keys sent N, buffer waits N, dropped N, release re-sends N`
  (also included in "Copy diagnostics").

### Changed
- Mac app (KVM-16): key reports are queued and sent in order, and only when the Bluetooth
  buffer has room. Before, they were written without checking and macOS can silently drop a
  write when its buffer is full.
- Mac app (KVM-16): a report is only sent when the key state changes. Auto-repeat events from
  holding a key no longer flood the link.
- Mac app (KVM-16): after the last key is released, the "all keys up" report is re-sent 40 ms
  and 150 ms later, so a lost release is corrected instead of waiting for the next key press.
- Board: the bottom of the screen is re-laid out to fit the counters.

### Status
- Unverified on hardware.

## [0.4.3] - 2026-10-06

Diagnostic build for KVM-13 (Mac app pairs, then the link drops). **No change to pairing or
keystroke behaviour.** It only makes failures readable. Also extends KVM-14.

### Added
- Board (KVM-13, KVM-14): the two bottom lines on every screen now show the previous and the
  latest Bluetooth event, each stamped with seconds since boot, plus `paired: N`. A dropped
  link now says why, using the Bluetooth reason code (`dropped: remote ended (0x13)`,
  `dropped: link timeout (0x08)`, ...). New events: `link up, not yet encrypted`,
  `app subscribed to state` (the Mac app really used the control service),
  `encryption failed (n)`, `repeat pairing: bond replaced`.
- Mac app (KVM-13): the menu shows the real Bluetooth error and keeps showing the last problem
  while it retries ("Connecting... (last: ...)") instead of only "Connecting...".
- Mac app (KVM-13): "Recent events" submenu with the latest Bluetooth steps, and a
  "Copy diagnostics" item that copies the full history (versions, status lines, events) to the
  clipboard for pasting into a ticket.
- Mac app (KVM-13): if the device does not accept a connection within 15 s, the menu says so.

### Status
- Unverified on hardware. Does not fix KVM-13; it is meant to show its cause.

## [0.4.2] - 2026-10-06

### Fixed
- The work laptop connected, then was disconnected a moment later (seen on the laptop's
  Bluetooth status; KVM-2 step 7). Suspected cause: the board identified a device by the
  address it used when encryption started, which can be a temporary address, so a returning
  device did not match and was refused. The board now waits briefly and uses the device's
  permanent address. This is a suspected fix and is unverified on hardware.

### Added
- Diagnostic line at the bottom of the WORK, GAME and MAC screens: what happened to the last
  connection (`refused: ...`, `dropped by host/link`, `paired: ...`) and how many devices are
  paired. It also prints to the serial output.

### Changed
- Pairings are reset the first time this version boots. Every device pairs again once: remove
  "Desk Keyboard" from the laptop's and the Mac's Bluetooth lists first, then pair the Mac app
  (MAC screen), the work laptop (WORK screen) and the gaming PC (GAME screen).

### Status
- Unverified on hardware.

## [0.4.1] - 2026-10-06

### Added
- Version numbers: a `VERSION` file, shown on the device boot screen and serial output, in the
  Mac app menu, and in the release names (`v0.4.1-b<build>`).
- The device reports its firmware version to the Mac app, which shows it in the menu so a
  mismatch between app and firmware is easy to spot.
- This changelog, and a CI check that `VERSION` matches the newest changelog entry.
- Release notes are now taken from this changelog.

## [0.4.0] - 2026-10-05

### Added
- Mac link: a second, passkey-protected Bluetooth service on the device. The Mac app sends
  keystrokes through it and can switch the target.
- Mac menu-bar app (`mac/`): captures the keyboard, forwards it in Work and Game modes, and
  switches target on a double-tap of Left Command (toward Game) or Right Command (toward Work).
- Device status text on every screen (`connected`, `waiting for host`, `Mac app connected`).
- CI builds the Mac app on a macOS runner and attaches `KVMBridge.zip` to releases.

### Changed
- The device now tracks several Bluetooth connections at once (Mac app plus one host).
  Each paired device has a role: Work host, Game host or Mac app.

### Status
- Unverified on hardware.

## [0.3.0] - 2026-10-05

### Added
- Bluetooth keyboard ("Desk Keyboard") with passkey pairing; the code is shown on the screen.
- One paired host per target (Work, Game); the other is disconnected.
- Hold the screen 1 s to type a test string, 4 s to forget the paired host.

### Status
- Verified: pairing with the work laptop and the test string typing.

## [0.2.0] - 2026-10-05

### Added
- Touch (FT6336G): tap the left half to move toward Game, the right half toward Work.
- The last target is remembered across power cycles.

### Status
- Verified: switching and saved state.

## [0.1.0] - 2026-10-05

### Added
- Display indicator: green MAC with a left arrow, red WORK with a right arrow, blue GAME with
  two left arrows. Text sits clear of the arrows.
- PlatformIO project for the Freenove ESP32-S3 2.8" CYD (ILI9341V display).
- CI: compile check on every push; manual build publishes `kvm-merged.bin` as a GitHub Release.

### Fixed
- Display colours were inverted on the real panel; inversion is now switched on.

### Status
- Verified: display, pins, colours, layout.
