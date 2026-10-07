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

// The USB-C cable. (Normally Serial is the board's UART0, used only for debug text.)
#if ARDUINO_USB_CDC_ON_BOOT
#define LINK Serial
#else
#define LINK USBSerial  // declared by the core when USB mode is on and Serial is not the USB port
#endif

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
int pendingSleep = -1;     // "@Z 1" = 1, "@Z 0" = 0, none = -1
volatile bool keySeen = false;  // a key report arrived since last asked
uint32_t linkLostAt = 0;  // when the link last went down (0 = since boot)
int pendingGoto = -1;
// Mouse (KVM-9): movement from the Mac app is summed here and sent to the PC at most every few ms;
// if a send fails the rest stays and is retried, so no movement is lost.
int mouseButtons = 0, sentButtons = 0;
long accDx = 0, accDy = 0, accW = 0, accP = 0;
bool mouseDirty = false;
uint32_t lastMouseSend = 0;  // target number asked for by "@G n", or -1
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

int8_t clamp8(long v) { return static_cast<int8_t>(v > 127 ? 127 : (v < -127 ? -127 : v)); }

// Sends the summed mouse movement to the PC in chunks of at most 127.
void pumpMouse() {
  if (!mouseDirty) return;
  if (!kbd::connected()) {  // nobody to send to: drop it
    accDx = accDy = accW = accP = 0;
    sentButtons = mouseButtons;
    mouseDirty = false;
    return;
  }
  const uint32_t now = millis();
  if (now - lastMouseSend < 6) return;
  const int8_t dx = clamp8(accDx), dy = clamp8(accDy), w = clamp8(accW), p = clamp8(accP);
  const bool buttonsChanged = (mouseButtons != sentButtons);
  if (dx == 0 && dy == 0 && w == 0 && p == 0 && !buttonsChanged) {
    mouseDirty = false;
    return;
  }
  if (kbd::relayMouse(static_cast<uint8_t>(mouseButtons), dx, dy, w, p)) {
    accDx -= dx;
    accDy -= dy;
    accW -= w;
    accP -= p;
    sentButtons = mouseButtons;
    lastMouseSend = now;
    mouseDirty = (accDx != 0 || accDy != 0 || accW != 0 || accP != 0);
  }
}

void handleLine(const char *l) {
  if (l[0] != '@') return;  // the firmware's own debug text
  lastHeard = millis();
  if (!linked) {
    linked = true;
    changedFlag = true;
  }
  switch (l[1]) {
    case 'M': {  // "@M <buttons> <dx> <dy> <wheel> <pan>": decimal numbers
      int b, dx, dy, w, p;
      if (sscanf(l + 2, "%d %d %d %d %d", &b, &dx, &dy, &w, &p) == 5) {
        mouseButtons = b & 7;
        accDx += dx;
        accDy += dy;
        accW += w;
        accP += p;
        mouseDirty = true;
      }
      break;
    }
    case 'Z':  // "@Z 1": the Mac is asleep, locked or off. "@Z 0": it is back.
      if (l[2] == ' ' && (l[3] == '0' || l[3] == '1')) pendingSleep = l[3] - '0';
      break;
    case 'K': {  // "@K " + 16 hex digits
      keySeen = true;
      uint8_t report[8];
      if (strlen(l) >= 19 && parseReport(l + 3, report)) {
        const bool ok = kbd::relayReport(report);
        // Echo for the Mac app's key trace (KVM-20): board time, sent to host or not, report.
        LINK.printf("@R %lu %c %.16s\n", static_cast<unsigned long>(millis()), ok ? '+' : '-', l + 3);
      }
      break;
    }
    case 'C': {  // "@C " + 4 hex digits: one media key usage, 0000 = released
      if (strlen(l) >= 7) {
        char buf[5] = {l[3], l[4], l[5], l[6], 0};
        bool ok = true;
        for (int i = 0; i < 4; i++) ok = ok && isxdigit(static_cast<unsigned char>(buf[i]));
        if (ok) kbd::relayConsumer(static_cast<uint16_t>(strtoul(buf, nullptr, 16)));
      }
      break;
    }
    case 'S':
      if (l[3] == 'L') pendingStep = -1;
      else if (l[3] == 'R') pendingStep = +1;
      break;
    case 'G':  // "@G n": go straight to target n (0 Mac, 1 Work, 2 Game)
      if (l[2] == ' ' && l[3] >= '0' && l[3] <= '2') pendingGoto = l[3] - '0';
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
  pumpMouse();
  if (linked && millis() - lastHeard > kLinkTimeoutMs) {
    linked = false;
    linkLostAt = millis();
    changedFlag = true;
  }
}

bool connected() { return linked; }

uint32_t lostForMs() { return linked ? 0 : millis() - linkLostAt; }

int takeSleepCmd() {
  const int s = pendingSleep;
  pendingSleep = -1;
  return s;
}

bool takeKeyActivity() {
  if (!keySeen) return false;
  keySeen = false;
  return true;
}

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

bool takeGoto(uint8_t &t) {
  if (pendingGoto < 0) return false;
  t = static_cast<uint8_t>(pendingGoto);
  pendingGoto = -1;
  return true;
}

void publishTarget(uint8_t t) {
  target = t;
  LINK.printf("@T %u\n", static_cast<unsigned>(t));
}

}  // namespace maclink
