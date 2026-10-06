#include "maclink.h"

#include "ble_kbd.h"

#include "HWCDC.h"

#ifndef KVM_VERSION
#define KVM_VERSION "dev"
#endif
#ifndef KVM_BUILD
#define KVM_BUILD "dev"
#endif

// The Mac link uses the chip's built-in USB Serial/JTAG port directly, so the normal Serial
// (debug text) stays separate. Fail the build, not the user, if it is not available.
#if !ARDUINO_USB_MODE
#error "ARDUINO_USB_MODE=1 is required (platformio.ini) for the USB link to the Mac app."
#endif

// The USB-C cable. (Serial itself is the board's UART0, used only for debug text.)
#define LINK HWCDCSerial

namespace maclink {
namespace {

constexpr size_t kMaxLine = 64;
constexpr uint32_t kLinkTimeoutMs = 3000;

char line[kMaxLine];
size_t len = 0;
bool overflow = false;

uint32_t lastHeard = 0;
bool linked = false;
volatile bool changedFlag = false;
int pendingStep = 0;
uint8_t target = 0;

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "0200040000000000" -> 8 bytes. Returns false on any non-hex character.
bool parseReport(const char *hex, uint8_t out[8]) {
  for (int i = 0; i < 8; i++) {
    const int hi = hexValue(hex[2 * i]);
    const int lo = hexValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

void sendState() {
  LINK.printf("@T %u\n", static_cast<unsigned>(target));
  LINK.printf("@V %s %s\n", KVM_VERSION, KVM_BUILD);
}

void handleLine(const char *l) {
  if (l[0] != '@') return;  // the firmware's own debug text
  lastHeard = millis();
  if (!linked) {
    linked = true;
    changedFlag = true;
  }
  switch (l[1]) {
    case 'K': {  // "@K " + 16 hex digits
      uint8_t report[8];
      if (strlen(l) >= 19 && parseReport(l + 3, report)) kbd::relayReport(report);
      break;
    }
    case 'S':
      if (l[3] == 'L') pendingStep = -1;
      else if (l[3] == 'R') pendingStep = +1;
      break;
    case 'H':
      sendState();
      break;
    default:
      break;
  }
}

}  // namespace

void begin() {
  LINK.begin();
  // Never block the main loop if the Mac is not reading the port.
  LINK.setTxTimeoutMs(0);
}

void poll() {
  while (LINK.available() > 0) {
    const int c = LINK.read();
    if (c < 0) break;
    if (c == '\n') {
      if (!overflow && len > 0) {
        line[len] = '\0';
        handleLine(line);
      }
      len = 0;
      overflow = false;
    } else if (c != '\r') {
      if (len < kMaxLine - 1) line[len++] = static_cast<char>(c);
      else overflow = true;
    }
  }
  if (linked && millis() - lastHeard > kLinkTimeoutMs) {
    linked = false;
    changedFlag = true;
  }
}

bool connected() { return linked; }

bool takeChanged() {
  if (!changedFlag) return false;
  changedFlag = false;
  return true;
}

bool takeStep(int &dir) {
  const int s = pendingStep;
  if (s == 0) return false;
  pendingStep = 0;
  dir = s;
  return true;
}

void publishTarget(uint8_t t) {
  target = t;
  LINK.printf("@T %u\n", static_cast<unsigned>(t));
}

}  // namespace maclink
