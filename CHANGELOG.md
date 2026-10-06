# Changelog

All notable changes to this project are recorded here. The version number lives in the
`VERSION` file and is shown on the device's boot screen, in the Mac app's menu and in release
names. Format follows [Keep a Changelog](https://keepachangelog.com/); versions follow
[Semantic Versioning](https://semver.org/) (while the major number is 0, a minor bump may
change behaviour).

"Verified" means checked on the real hardware. "Unverified" means it compiles in CI but has not
been tried on the board yet.

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
