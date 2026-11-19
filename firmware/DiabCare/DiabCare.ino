// ============================================================================
// DiabCare - MYOSA wearable for diabetic mobility monitoring and foot-risk awareness.
// Core 1 (loop): 100 Hz sensing, steps/bouts/gait features, personal-baseline
//                analysis, follow-up indicator, OLED. Never depends on the network.
// Core 0 (task): Wi-Fi + Firebase upload with flash store-and-forward (cloud.h).
// Indicator-based awareness tool: NOT a medical device, NOT plantar pressure.
// ============================================================================
#include <Wire.h>
#include <ArduinoJson.h>
#include "config.h"
#include "types.h"
#include "gait.h"
#include "analysis.h"
#include "demo_data.h"
#include "signal_processing.h"
#include "classifier.h"
#include "sensors.h"
#include "display.h"
#include "storage.h"
#include "cloud.h"        // needs secrets.h (copy secrets.example.h)

Imu imu;
SignalProc sp;
GaitEngine gait;
AnalysisEngine eng, demoEng;
ActivityConfirm confirm;
Display disp;
Cloud cloud;

bool sensorError = false, demo = false, usedModel = false;
uint8_t imuFail = 0;
uint32_t lastImuUs = 0, lastSecMs = 0, lastRetryMs = 0, lastPersistMs = 0, lastDemoMs = 0;
int demoDay = 0;
float lastCadence = 0;

static AnalysisEngine& active() { return demo ? demoEng : eng; }
static uint32_t localDay() { uint32_t e = Cloud::epoch(); return e ? (e + TZ_OFFSET_S) / 86400UL : 0; }
static String dayPath(uint32_t day, bool isDemo) {
  return String("/patients/") + PATIENT_ID + (isDemo ? "/demo/daily/" : "/daily/") + day;
}

static void sendBout(const BoutFeatures& b, const BoutVerdict& v) {
  JsonDocument d; char buf[512];
  d["ts"] = Cloud::epoch(); d["leg"] = String(b.leg); d["steps"] = b.steps; d["dur"] = (int)b.durS;
  d["cad"] = (int)b.cadence; d["strideT"] = roundf(b.strideMean * 1000) / 1000; d["strideCV"] = roundf(b.strideCV * 100) / 100;
  d["reg"] = roundf(b.regularity * 1000) / 1000; d["ahs"] = roundf(b.ahs * 1000) / 1000; d["lr"] = roundf(b.loadRate * 10) / 10;
  if (!isnan(v.dm)) d["dm"] = roundf((float)v.dm * 100) / 100;
  d["abn"] = v.abnormal; d["asym"] = v.asymMask;
  serializeJson(d, buf, sizeof buf);
  cloud.send("POST", (String("/patients/") + PATIENT_ID + "/bouts").c_str(), buf);
}

static void sendDaily(const DailySummary& s, const AnalysisEngine& e, bool isDemo) {
  JsonDocument d; char buf[512];
  d["day"] = s.day; d["ts"] = Cloud::epoch(); d["cli"] = (int)s.cli; d["abn"] = roundf(s.abnFrac * 100) / 100;
  d["mask"] = s.mask; d["risk"] = s.risk; d["riskName"] = levelName(s.risk); d["baseDone"] = s.baseDone; d["demo"] = isDemo;
  JsonArray m = d["met"].to<JsonArray>(); for (int i = 0; i < NMET; i++) m.add(roundf(s.met[i] * 10) / 10);
  JsonArray sy = d["sym"].to<JsonArray>(); for (int i = 0; i < NSYM; i++) sy.add(roundf(s.sym[i] * 10) / 10);
  JsonArray bm = d["bm"].to<JsonArray>(); for (int i = 0; i < NMET; i++) bm.add(roundf((float)e.s.dayBase[i].mean * 10) / 10);
  JsonArray bs = d["bs"].to<JsonArray>(); for (int i = 0; i < NMET; i++) bs.add(roundf((float)e.sigma(i) * 10) / 10);
  serializeJson(d, buf, sizeof buf);
  cloud.send("PUT", dayPath(s.day, isDemo).c_str(), buf);
}

static void persistNow() { stateSave(eng.s); lastPersistMs = millis(); }

static void startDemo() {
  demo = true; demoDay = 0; demoEng.reset(); demoEng.minBoutsForBaseline = 0;
  cloud.send("DELETE", (String("/patients/") + PATIENT_ID + "/demo").c_str(), "");
  lastDemoMs = millis();
}

static void tryInit() {
  if (imu.begin()) { sensorError = false; sp.reset(); gait.reset(); imuFail = 0; lastImuUs = micros(); }
}

static void onWindow() {
  ClassResult r = classifyWindow(sp.interleaved(), sp.rms, gait.recentStrides());
  usedModel = r.usedModel;
  confirm.onWindow(r.cls);
}

static void publishLive() {
  AnalysisEngine& e = active();
  JsonDocument d, ix; char a[640], b[160];
  d["ts"] = Cloud::epoch(); d["up"] = (uint32_t)(millis() / 1000); d["fw"] = FW_VERSION;
  d["err"] = sensorError; d["demo"] = demo; d["model"] = usedModel; d["rssi"] = cloud.wifiUp() ? WiFi.RSSI() : 0;
  d["leg"] = String(eng.s.curLeg); d["act"] = (int)confirm.confirmed;
  d["steps"] = demo ? 0 : eng.s.steps + gait.pendingSteps(); d["cad"] = (int)lastCadence;
  d["risk"] = e.s.level; d["riskName"] = levelName(e.s.level);
  d["base"] = e.baselinePct(); d["baseDone"] = e.s.baseDone;
  d["asym"] = e.s.liveAsymMask; d["load"] = (bool)e.s.liveLoadFlag; d["queued"] = cloud.queued();
  serializeJson(d, a, sizeof a);
  ix["risk"] = e.s.level; ix["ts"] = Cloud::epoch(); ix["base"] = e.baselinePct(); ix["demo"] = demo; ix["err"] = sensorError;
  serializeJson(ix, b, sizeof b);
  cloud.setLive(a, b, confirm.confirmed == ACT_WALKING);
}

static void updateUi() {
  AnalysisEngine& e = active();
  UiState u{confirm.confirmed, demo ? 0 : eng.s.steps + gait.pendingSteps(), lastCadence, eng.s.curLeg,
            (bool)e.s.baseDone, (uint8_t)min<int>(e.s.baseDays, 255),
            (uint8_t)AnalysisEngine::popcount16(e.s.liveAsymMask), (bool)e.s.liveLoadFlag,
            e.s.level, demo, cloud.wifiUp(), (uint16_t)cloud.queued()};
  disp.show(u);
}

static void onSecond() {
  eng.onActivitySecond(confirm.confirmed);

  // day roll (needs a synced clock; without NTP the day simply continues)
  uint32_t day = localDay();
  if (day) {
    if (eng.s.curDay == 0) eng.s.curDay = day;
    else if (day != eng.s.curDay) {
      DailySummary d = eng.onDayEnd();
      sendDaily(d, eng, false);
      eng.s.curDay = day; persistNow();
    }
  }

  if (demo && millis() - lastDemoMs >= DEMO_DAY_PERIOD_MS && demoDay < DEMO_DAYS) {
    lastDemoMs = millis(); demoDay++;
    DailySummary d; demoSummary(demoDay, d);
    d = demoEng.processDay(d);
    sendDaily(d, demoEng, true);
  }

  if (millis() - lastPersistMs >= PERSIST_PERIOD_MS) persistNow();
  updateUi();
  publishLive();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(I2C_CLOCK);
  disp.begin();
  storageBegin();
  if (!stateLoad(eng.s)) eng.reset();               // restores baseline + history after a reboot
  if (eng.s.curLeg != 'L' && eng.s.curLeg != 'R') eng.s.curLeg = DEFAULT_LEG;
  cloud.begin();
  tryInit();
  if (!imu.ping()) sensorError = true;
  lastImuUs = micros(); lastSecMs = millis(); lastPersistMs = millis();
}

void loop() {
  uint32_t nowMs = millis();

  // commands from the Flutter app / Serial: l / r = leg, d = demo on/off, b = reset baseline
  if (cloud.cmdLeg) { eng.setLeg(cloud.cmdLeg); gait.leg = eng.s.curLeg; cloud.cmdLeg = 0; }
  if (cloud.cmdDemo >= 0) { if (cloud.cmdDemo == 1) startDemo(); else demo = false; cloud.cmdDemo = -1; }
  if (cloud.cmdResetBaseline) { eng.resetBaseline(); persistNow(); cloud.cmdResetBaseline = false; }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'l' || c == 'r') { eng.setLeg(c == 'l' ? 'L' : 'R'); gait.leg = eng.s.curLeg; }
    else if (c == 'd') { if (demo) demo = false; else startDemo(); }
  }
  gait.leg = eng.s.curLeg;

  if (sensorError) {
    if (nowMs - lastRetryMs >= 2000) { lastRetryMs = nowMs; tryInit(); disp.showError("MPU6050"); publishLive(); }
    return;
  }

  uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastImuUs) >= IMU_PERIOD_US) {
    lastImuUs += IMU_PERIOD_US;
    if ((uint32_t)(nowUs - lastImuUs) > 200000UL) lastImuUs = nowUs;      // catch-up guard
    ImuSample s;
    if (imu.read(s)) {
      imuFail = 0;
      bool win = sp.push(s.ax, s.ay, s.az);
      float w = GYRO_SIGN * (GYRO_SAGITTAL_AXIS == 0 ? s.gx : GYRO_SAGITTAL_AXIS == 1 ? s.gy : s.gz);
      gait.push(w, sp.lastDyn, sp.lastMag);
      if (win) onWindow();
      BoutFeatures b;
      while (gait.takeBout(b)) {
        BoutVerdict v = eng.onBout(b);
        lastCadence = b.cadence;
        sendBout(b, v);
      }
    } else if (++imuFail >= 20) { sensorError = true; }
  }

  if (nowMs - lastSecMs >= 1000) { lastSecMs += 1000; onSecond(); }
}
