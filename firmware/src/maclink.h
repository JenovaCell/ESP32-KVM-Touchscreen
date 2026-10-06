// USB link to the Mac app.
//
// The board's USB-C port shows up on the Mac as a serial port. The Mac app sends one text line
// at a time; any line that does not start with '@' is ignored (it is the firmware's own debug text).
//
//   Mac to board:  @H            heartbeat, about once a second (the board answers with @T and @V)
//                  @K <16 hex>   one 8-byte keyboard report, e.g. @K 0200040000000000
//                  @S L | @S R   step the target left (toward GAME) or right (toward WORK)
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

// True once after the link came up or went down.
bool takeChanged();

// True once when the Mac app asked to switch target: dir = -1 (left) or +1 (right).
bool takeStep(int &dir);

// Tell the Mac app the active target: 0 = Mac, 1 = Work, 2 = Game.
void publishTarget(uint8_t target);

}  // namespace maclink
