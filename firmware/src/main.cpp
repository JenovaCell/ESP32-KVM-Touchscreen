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

static void drawTarget(Target t) {
  const bool mac = (t == Target::Mac);
  const uint16_t bg = mac ? TFT_NAVY : TFT_DARKGREEN;
  const char *label = mac ? "MAC" : "WORK";
  const char *hint = mac ? "keyboard -> this Mac" : "keyboard -> work laptop";

  tft.fillScreen(bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, tft.width() / 2, tft.height() / 2 - 10, 6);
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
