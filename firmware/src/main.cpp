// Display + touch + Bluetooth keyboard for the hosts + USB link to the Mac app.
// Shows the active target (MAC / WORK / GAME). Tap the left half of the screen
// to move one step left (toward GAME), the right half to move one step right
// (toward WORK). The last target is remembered across power cycles.
// On WORK or GAME the device is a Bluetooth keyboard for that host. Pairing
// shows a passkey on the screen. The Mac app connects over a second secured link,
// sends keystrokes and can switch the target.
//   Hold 1 s:  type a test string into the connected host.
//   Hold 4 s:  forget the paired host for this target (to pair a new one).

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <Wire.h>

#include "ble_kbd.h"
#include "maclink.h"

#if KVM_BACKLIGHT_PIN < 0 && !defined(KVM_CI_COMPILE_ONLY)
#error "Set the pins in platformio.ini (BOARD VALUES) before building."
#endif

#ifndef KVM_VERSION
#define KVM_VERSION "dev"
#endif
#ifndef KVM_BUILD
#define KVM_BUILD "dev"
#endif
#ifndef KVM_SHA
#define KVM_SHA "unknown"
#endif

#ifndef KVM_TOUCH_FLIP_X
#define KVM_TOUCH_FLIP_X 0  // set to 1 if left/right taps feel reversed
#endif

#ifndef KVM_INVERT_DISPLAY
#define KVM_INVERT_DISPLAY 0
#endif

enum class Target { Mac, Work, Game };

struct TargetStyle {
  const char *label;
  const char *hint;
  uint8_t r, g, b;   // background colour
  bool pointLeft;    // arrow side
  int heads;         // 1 = single arrow, 2 = double arrow
};

// Mac: green, one arrow left. Work: red, one arrow right. Game: blue, two arrows left.
static const TargetStyle kStyles[] = {
    {"MAC", "keyboard -> this Mac", 0, 140, 60, true, 1},
    {"WORK", "keyboard -> work laptop", 190, 30, 30, false, 1},
    {"GAME", "keyboard -> gaming PC", 0, 80, 200, true, 2},
};

static TFT_eSPI tft;

// Arrow geometry (pixels).
static const int kMargin = 10, kHead = 28, kHalfHead = 34, kHalfShaft = 11;
static const int kShaft = 32;      // shaft length behind the last head
static const int kHeadPitch = 40;  // tip-to-tip spacing for a double arrow

static int arrowWidth(int heads) { return kHead + kShaft + (heads - 1) * kHeadPitch; }

// Block arrow at the screen edge; `heads` triangles in a row, shaft behind the last one.
static void drawArrow(bool pointLeft, int heads, uint16_t color) {
  const int cy = tft.height() / 2;
  // Work in "distance from the edge" and mirror for a right-pointing arrow.
  auto x = [&](int d) { return pointLeft ? kMargin + d : tft.width() - kMargin - d; };
  for (int i = 0; i < heads; i++) {
    const int tip = i * kHeadPitch, base = tip + kHead;
    tft.fillTriangle(x(tip), cy, x(base), cy - kHalfHead, x(base), cy + kHalfHead, color);
  }
  const int shaftStart = (heads - 1) * kHeadPitch + kHead;
  const int x0 = x(shaftStart), x1 = x(shaftStart + kShaft);
  tft.fillRect(min(x0, x1), cy - kHalfShaft, kShaft, kHalfShaft * 2, color);
}

static uint16_t gBg = 0;  // background colour of the current screen

// Shows how many key reports the board has relayed (WORK and GAME screens only).
static void drawCounters(Target t) {
  if (t == Target::Mac) return;
  const kbd::KeyStats k = kbd::keyStats();
  const int y = tft.height() - 28;
  char buf[56];
  snprintf(buf, sizeof(buf), "keys rx %lu tx %lu fail %lu", static_cast<unsigned long>(k.rx),
           static_cast<unsigned long>(k.txOk), static_cast<unsigned long>(k.txFail));
  tft.fillRect(0, y - 5, tft.width(), 10, gBg);
  tft.setTextColor(TFT_WHITE, gBg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.drawString(buf, tft.width() / 2, y, 1);
}

static void drawTarget(Target t) {
  const TargetStyle &s = kStyles[static_cast<int>(t)];
  const uint16_t bg = tft.color565(s.r, s.g, s.b);
  gBg = bg;

  // Centre the text in the space the arrow leaves free (shifted away from it).
  const int free0 = s.pointLeft ? kMargin + arrowWidth(s.heads) : 0;
  const int free1 = s.pointLeft ? tft.width() : tft.width() - kMargin - arrowWidth(s.heads);
  const int cx = (free0 + free1) / 2;

  tft.fillScreen(bg);
  drawArrow(s.pointLeft, s.heads, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  // Font 6 only has digits, so letters need font 4 scaled up.
  tft.setTextSize(2);
  tft.drawString(s.label, cx, tft.height() / 2 - 10, 4);
  tft.setTextSize(1);
  tft.drawString(s.hint, cx, tft.height() / 2 + 50, 2);
  const char *status;
  if (t == Target::Mac) status = maclink::connected() ? "Mac app connected (USB)" : "waiting for Mac app (USB)";
  else status = kbd::connected() ? "connected" : "waiting for host";
  tft.drawString(status, cx, tft.height() - 40, 2);
  // Diagnostics: the previous and the latest Bluetooth event, centred on the screen.
  char diag[64];
  snprintf(diag, sizeof(diag), "%.50s", kbd::prevEvent());
  tft.drawString(diag, tft.width() / 2, tft.height() - 18, 1);
  snprintf(diag, sizeof(diag), "%.38s | paired: %d", kbd::lastEvent(), kbd::bondCount());
  tft.drawString(diag, tft.width() / 2, tft.height() - 9, 1);
  drawCounters(t);
}

static void drawSplash() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.drawString("KVM", tft.width() / 2, tft.height() / 2 - 30, 4);
  tft.drawString("v" KVM_VERSION, tft.width() / 2, tft.height() / 2 + 5, 4);
  tft.drawString(KVM_BUILD " " KVM_SHA, tft.width() / 2, tft.height() / 2 + 40, 2);
}

static void drawPasskey(uint32_t code) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.drawString("Enter this code on the host", tft.width() / 2, 60, 2);
  tft.setTextSize(2);
  char buf[8];
  snprintf(buf, sizeof(buf), "%06lu", static_cast<unsigned long>(code));
  tft.drawString(buf, tft.width() / 2, tft.height() / 2 + 10, 4);
  tft.setTextSize(1);
}

// --- Touch (FT6336G over I2C) ------------------------------------------------

static const uint8_t kFt6336Addr = 0x38;

// Reads the first touch point in the panel's native portrait coordinates
// (x 0..239, y 0..319). Returns false if nothing is touching or on I2C error.
static bool readTouch(int &x, int &y) {
  Wire.beginTransmission(kFt6336Addr);
  Wire.write(0x02);  // TD_STATUS, then P1 XH/XL/YH/YL
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kFt6336Addr, (uint8_t)5) != 5) return false;
  const uint8_t n = Wire.read() & 0x0F;
  const uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (n == 0 || n > 2) return false;
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return true;
}

static void touchInit() {
  pinMode(KVM_TOUCH_RST, OUTPUT);
  digitalWrite(KVM_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(KVM_TOUCH_RST, HIGH);
  delay(300);
  Wire.begin(KVM_TOUCH_SDA, KVM_TOUCH_SCL, 400000);
}

// --- Target selection ----------------------------------------------------------

// Physical left-to-right order, matching the arrows on screen.
static const Target kSpatial[] = {Target::Game, Target::Mac, Target::Work};
static const int kSpatialCount = sizeof(kSpatial) / sizeof(kSpatial[0]);

static Preferences prefs;
static int spatialPos = 1;  // index into kSpatial; starts on Mac

static int posOf(Target t) {
  for (int i = 0; i < kSpatialCount; i++)
    if (kSpatial[i] == t) return i;
  return 1;
}

static void showCurrent() { drawTarget(kSpatial[spatialPos]); }

static kbd::Slot slotFor(Target t) {
  switch (t) {
    case Target::Work: return kbd::Slot::Work;
    case Target::Game: return kbd::Slot::Game;
    default: return kbd::Slot::None;
  }
}

// Move to position `next` in kSpatial (does nothing if already there).
static void moveTo(int next) {
  if (next == spatialPos) return;
  spatialPos = next;
  const Target t = kSpatial[spatialPos];
  kbd::setSlot(slotFor(t));
  maclink::publishTarget(static_cast<uint8_t>(t));
  showCurrent();
  prefs.putUChar("target", static_cast<uint8_t>(t));
  Serial.println(kStyles[static_cast<int>(t)].label);
}

// dir: -1 = left, +1 = right. Stops at the ends (no wrap).
static void step(int dir) { moveTo(constrain(spatialPos + dir, 0, kSpatialCount - 1)); }

void setup() {
  Serial.begin(115200);

  pinMode(KVM_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(KVM_BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.invertDisplay(KVM_INVERT_DISPLAY);
  tft.setRotation(1);  // landscape; change to 0/2/3 if it is the wrong way up

  drawSplash();
  Serial.println("KVM v" KVM_VERSION " (" KVM_BUILD " " KVM_SHA ")");
  const uint32_t splashStart = millis();

  touchInit();

  prefs.begin("kvm", false);
  const uint8_t saved = prefs.getUChar("target", static_cast<uint8_t>(Target::Mac));
  if (saved < kSpatialCount) spatialPos = posOf(static_cast<Target>(saved));
  maclink::begin();
  kbd::begin();
  while (millis() - splashStart < 1200) delay(10);  // keep the splash readable
  kbd::setSlot(slotFor(kSpatial[spatialPos]));
  maclink::publishTarget(static_cast<uint8_t>(kSpatial[spatialPos]));
  showCurrent();
  Serial.println("stage 3b: display + touch + bluetooth up");
}

void loop() {
  static bool wasDown = false;
  static uint32_t downAt = 0, lastRelease = 0;
  static int downScreenX = 0;

  maclink::poll();
  kbd::poll();

  uint32_t code;
  if (kbd::takePasskey(code)) drawPasskey(code);
  // `|` not `||`: both flags must be read every time.
  if (kbd::takeChanged() | maclink::takeChanged()) showCurrent();  // also ends the pairing overlay
  int macStep;
  if (maclink::takeStep(macStep)) step(macStep);
  uint8_t macGoto;
  if (maclink::takeGoto(macGoto) && macGoto < kSpatialCount) moveTo(posOf(static_cast<Target>(macGoto)));

  int rawX, rawY;
  const bool down = readTouch(rawX, rawY);
  const uint32_t now = millis();

  // Refresh the key counters at most every 400 ms, and only when they changed.
  static uint32_t lastCounterDraw = 0, lastRx = 0;
  const uint32_t rxNow = kbd::keyStats().rx;
  if (rxNow != lastRx && now - lastCounterDraw > 400) {
    lastRx = rxNow;
    lastCounterDraw = now;
    drawCounters(kSpatial[spatialPos]);
  }

  if (down && !wasDown && now - lastRelease > 150) {
    // In landscape the screen's horizontal axis is the panel's native Y axis.
    downScreenX = KVM_TOUCH_FLIP_X ? (tft.width() - 1 - rawY) : rawY;
    downAt = now;
  }
  if (!down && wasDown) {
    lastRelease = now;
    const uint32_t held = now - downAt;
    if (held >= 4000) {
      Serial.println("forget host");
      kbd::forgetCurrentHost();
    } else if (held >= 1000) {
      Serial.println("type test");
      kbd::typeText("KVM test OK\n");
    } else {
      step(downScreenX < tft.width() / 2 ? -1 : +1);
    }
  }
  wasDown = down;

  delay(3);  // short, so keystrokes from the Mac are relayed quickly
}
