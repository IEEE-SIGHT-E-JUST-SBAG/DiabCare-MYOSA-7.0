#pragma once
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "types.h"

struct UiState {
  ActivityClass act; uint32_t steps; float cadence; char leg;
  bool baseDone; uint8_t baseDays; uint8_t asymCount; bool loadFlag;
  uint8_t level; bool demo, wifi; uint16_t queued;
};

class Display {
 public:
  bool ok = false;
  void begin() {
    ok = oled_.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    if (!ok) return;
    oled_.clearDisplay(); oled_.setTextColor(SSD1306_WHITE);
    oled_.setTextSize(2); oled_.setCursor(14, 14); oled_.println("DiabCare");
    oled_.setTextSize(1); oled_.setCursor(8, 40); oled_.println("MYOSA gait monitor");
    oled_.display();
  }
  void showError(const char* what) {
    if (!ok) return;
    oled_.clearDisplay(); oled_.setTextSize(2); oled_.setCursor(0, 8); oled_.println("Sensor Error");
    oled_.setTextSize(1); oled_.setCursor(0, 36); oled_.printf("Fault: %s", what);
    oled_.setCursor(0, 48); oled_.println("Retrying..."); oled_.display();
  }
  void show(const UiState& u) {
    if (!ok) return;
    oled_.clearDisplay(); oled_.setTextColor(SSD1306_WHITE); oled_.setTextSize(1);
    oled_.setCursor(0, 0);  oled_.printf("%s %c %c%c", u.demo ? "DEMO" : "Diab", u.leg, u.wifi ? 'W' : '-', u.queued ? 'q' : ' ');
    oled_.setCursor(0, 10); oled_.printf("%s  Steps %lu", actName(u.act), (unsigned long)u.steps);
    oled_.setCursor(0, 20);
    if (!u.baseDone) oled_.printf("Learning baseline %u/%d d", u.baseDays, BASELINE_DAYS);
    else oled_.printf("Asym:%u Load:%s Cad:%.0f", u.asymCount, u.loadFlag ? "!" : "ok", u.cadence);
    oled_.setTextSize(2); oled_.setCursor(0, 31);
    oled_.print(!u.baseDone ? "LEARNING" : levelName(u.level));
    oled_.setTextSize(1); oled_.setCursor(0, 50);
    if (!u.baseDone) oled_.print("Walk as usual");
    else if (u.level == LEVEL_STABLE) oled_.print("Monitoring continues");
    else if (u.level == LEVEL_WATCH) oled_.print("Changes noted");
    else { oled_.print("Consider clinical"); oled_.setCursor(0, 58); oled_.print("follow-up"); }
    oled_.display();
  }
 private:
  Adafruit_SSD1306 oled_{128, 64, &Wire, -1};
};
