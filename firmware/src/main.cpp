// Stage 1: display only.
// Draws a large MAC / WORK indicator and flips between them every few seconds
// so you can confirm the panel, colours, rotation and backlight are right.
// No input, no Bluetooth yet.

#include <Arduino.h>
#include <TFT_eSPI.h>

#if KVM_BACKLIGHT_PIN < 0
#error "Set the pins in platformio.ini (BOARD VALUES) before building."
#endif

enum class Target { Mac, Work };

static TFT_eSPI tft;

// Block arrow at the screen edge: left = Mac, right = Work.
static void drawArrow(bool pointLeft, uint16_t color) {
  const int w = tft.width(), cy = tft.height() / 2;
  const int margin = 10, len = 60, head = 28, halfHead = 34, halfShaft = 11;
  if (pointLeft) {
    const int tip = margin, base = margin + head, tail = margin + len;
    tft.fillTriangle(tip, cy, base, cy - halfHead, base, cy + halfHead, color);
    tft.fillRect(base, cy - halfShaft, tail - base, halfShaft * 2, color);
  } else {
    const int tip = w - margin, base = w - margin - head, tail = w - margin - len;
    tft.fillTriangle(tip, cy, base, cy - halfHead, base, cy + halfHead, color);
    tft.fillRect(tail, cy - halfShaft, base - tail, halfShaft * 2, color);
  }
}

static void drawTarget(Target t) {
  const bool mac = (t == Target::Mac);
  const uint16_t bg = mac ? tft.color565(0, 140, 60) : tft.color565(190, 30, 30);
  const char *label = mac ? "MAC" : "WORK";
  const char *hint = mac ? "keyboard -> this Mac" : "keyboard -> work laptop";

  tft.fillScreen(bg);
  drawArrow(mac, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  // Font 6 only has digits, so letters need font 4 scaled up.
  tft.setTextSize(2);
  tft.drawString(label, tft.width() / 2, tft.height() / 2 - 10, 4);
  tft.setTextSize(1);
  tft.drawString(hint, tft.width() / 2, tft.height() / 2 + 50, 2);
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
  static Target t = Target::Mac;
  delay(3000);
  t = (t == Target::Mac) ? Target::Work : Target::Mac;
  drawTarget(t);
  Serial.println(t == Target::Mac ? "MAC" : "WORK");
}
