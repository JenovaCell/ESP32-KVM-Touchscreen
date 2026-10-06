// Bluetooth for the KVM device:
//  - a HID keyboard that presents to one host at a time (work laptop or gaming PC)
//  - a control service the Mac app uses to send keystrokes and switch targets
// Every link uses passkey pairing (code shown on the screen), so only someone
// who can see the device can pair with it.
#pragma once

#include <Arduino.h>

namespace kbd {

enum class Slot : int8_t { None = -1, Work = 0, Game = 1 };

void begin();

// Which host may be connected right now. None (Mac mode) disconnects HID hosts.
void setSlot(Slot s);

// Call every loop: applies connection policy and manages advertising.
void poll();

// True when the HID host for the current slot is connected and encrypted.
bool connected();

// True when the Mac app is connected and paired.
bool macConnected();

// Short text for the screen: what happened to the last connection, and how many
// devices are paired with the board.
const char *lastEvent();
const char *prevEvent();  // the event before that
int bondCount();

// Counters for keystroke reports relayed from the Mac app to the host (for diagnosis).
struct KeyStats {
  uint32_t rx;       // 8-byte key reports received from the Mac app
  uint32_t txOk;     // notifications sent to the host
  uint32_t txFail;   // notifications that reported failure (only detectable on some library versions)
  uint32_t noHost;   // reports received while no host was connected
  uint32_t badSize;  // reports with the wrong size
};
KeyStats keyStats();

// True once per pairing attempt; `passkey` is the 6-digit code to show on screen.
bool takePasskey(uint32_t &passkey);

// True once after a connection state changed (or a pairing attempt ended).
bool takeChanged();

// True once when the Mac app asked to switch target: dir = -1 (left) or +1 (right).
bool takeStep(int &dir);

// Tell the Mac app the active target: 0 = Mac, 1 = Work, 2 = Game.
void publishTarget(uint8_t target);

// Types letters, digits, spaces and newlines. Other characters are skipped.
void typeText(const char *text);

// Forgets the paired device for the current target (Mac screen = the Mac app).
void forgetCurrentHost();

}  // namespace kbd
