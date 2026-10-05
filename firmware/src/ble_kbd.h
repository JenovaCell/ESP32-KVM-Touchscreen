// BLE HID keyboard that presents to one host at a time (work laptop or gaming PC).
// Pairing uses a passkey shown on the screen, so only someone who can see the
// device can pair with it.
#pragma once

#include <Arduino.h>

namespace kbd {

enum class Slot : int8_t { None = -1, Work = 0, Game = 1 };

void begin();

// Which host may be connected right now. None (Mac mode) disconnects and stops advertising.
void setSlot(Slot s);

// Call every loop: applies connection policy and manages advertising.
void poll();

// True when the host for the current slot is connected and encrypted.
bool connected();

// True once per pairing attempt; `passkey` is the 6-digit code to show on screen.
bool takePasskey(uint32_t &passkey);

// True once after the connection state changed (or a pairing attempt ended).
bool takeChanged();

// Types letters, digits, spaces and newlines. Other characters are skipped.
void typeText(const char *text);

// Forgets the paired host for the current slot so a new host can pair.
void forgetCurrentHost();

}  // namespace kbd
