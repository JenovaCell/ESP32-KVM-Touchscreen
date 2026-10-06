# Changelog

All notable changes to this project are recorded here. The version number lives in the
`VERSION` file and is shown on the device's boot screen, in the Mac app's menu and in release
names. Format follows [Keep a Changelog](https://keepachangelog.com/); versions follow
[Semantic Versioning](https://semver.org/) (while the major number is 0, a minor bump may
change behaviour).

"Verified" means checked on the real hardware. "Unverified" means it compiles in CI but has not
been tried on the board yet.

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
