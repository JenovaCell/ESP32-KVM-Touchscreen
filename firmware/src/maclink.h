// USB link to the Mac app.
//
// The board's USB-C port shows up on the Mac as a serial port. The Mac app sends one text line
// at a time; any line that does not start with '@' is ignored (it is the firmware's own debug text).
//
//   Mac to board:  @H            heartbeat, about once a second (the board answers with @T and @V)
//                  @K <16 hex>   one 8-byte keyboard report, e.g. @K 0200040000000000
//                  @C <4 hex>    one media key (HID consumer usage), 0000 = released
//                  @S L | @S R   step the target left (toward GAME) or right (toward WORK)
//                  @Z <1|0>      the Mac is asleep, locked or off (1) / awake again (0)
//                  @G <0|1|2>    go straight to a target: 0 = Mac, 1 = Work, 2 = Game
//   Board to Mac:  @T <0|1|2>    active target: 0 = Mac, 1 = Work, 2 = Game
//                  @V <ver> <b>  firmware version and build
#pragma once

#include <Arduino.h>

namespace maclink {

void begin();

// Call every loop: reads the serial port and acts on complete lines.
void poll();

// True while the Mac app has been heard from in the last 3 seconds.
bool connected();

// Milliseconds since the Mac link went down (0 while it is up).
uint32_t lostForMs();

// -1 = nothing new, 1 = the Mac said it is asleep, 0 = the Mac said it is awake again.
int takeSleepCmd();

// True once when a key report arrived from the Mac (the Mac is in use).
bool takeKeyActivity();

// True once after the link came up or went down.
bool takeChanged();

// True once when the Mac app asked to switch target: dir = -1 (left) or +1 (right).
bool takeStep(int &dir);

// True once when the Mac app asked for a specific target (0 Mac, 1 Work, 2 Game).
bool takeGoto(uint8_t &target);

// Tell the Mac app the active target: 0 = Mac, 1 = Work, 2 = Game.
void publishTarget(uint8_t target);

}  // namespace maclink
