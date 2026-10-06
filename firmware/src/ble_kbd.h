// Bluetooth for the KVM device: a HID keyboard that presents to one host at a time
// (work laptop or gaming PC). The Mac app is not a Bluetooth device any more; it sends key
// reports over the USB cable (see maclink.h) and this module relays them to the host.
// Pairing uses a passkey shown on the screen, so only someone who can see the device
// can pair with it.
#pragma once

#include <Arduino.h>

namespace kbd {

enum class Slot : int8_t { None = -1, Work = 0, Game = 1 };

void begin();

// Which host may be connected right now. None (MAC screen) disconnects every host and stops advertising.
void setSlot(Slot s);

// Call every loop: applies connection policy and manages advertising.
void poll();

// True when the HID host for the current slot is connected and encrypted.
bool connected();

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

// Sends one 8-byte HID report (modifiers, reserved, 6 keys) to the connected host.
// Returns false if no host is connected or the send failed.
bool relayReport(const uint8_t *report);

// Sends one media key usage (HID consumer page, for example 0xCD play/pause, 0xE9 volume up;
// 0 = released) to the connected host. Returns false if no host is connected.
bool relayConsumer(uint16_t usage);

// Types letters, digits, spaces and newlines. Other characters are skipped.
void typeText(const char *text);

// Forgets the paired host for the current target (does nothing on the MAC screen).
void forgetCurrentHost();

}  // namespace kbd
