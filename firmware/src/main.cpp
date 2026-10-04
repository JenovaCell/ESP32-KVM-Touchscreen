// Stage 1: display only.
// Draws a large indicator for the active target and cycles through them every
// few seconds so you can confirm the panel, colours, rotation and backlight
// are right. No input, no Bluetooth yet.

#include <Arduino.h>
#include <TFT_eSPI.h>

#if KVM_BACKLIGHT_PIN < 0
#error "Set the pins in platformio.ini (BOARD VALUES) before building."
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

static void drawTarget(Target t) {
  const TargetStyle &s = kStyles[static_cast<int>(t)];
  const uint16_t bg = tft.color565(s.r, s.g, s.b);

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
}

void setup() {
  Serial.begin(115200);

  pinMode(KVM_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(KVM_BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);  // landscape; change to 0/2/3 if it is the wrong way up
  drawTarget(Target::Mac);
  Serial.println("stage 1: display up");
}

void loop() {
  static int i = 0;
  delay(3000);
  i = (i + 1) % 3;
  drawTarget(static_cast<Target>(i));
  Serial.println(kStyles[i].label);
}
