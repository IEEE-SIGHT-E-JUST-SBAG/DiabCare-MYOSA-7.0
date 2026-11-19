#pragma once
// IoT layer: Wi-Fi station + Firebase Realtime Database over REST (HTTPS).
//  * per-device authentication: Firebase Auth e-mail/password -> ID token (auto refresh)
//  * live status (best effort), bouts + daily summaries through a flash store-and-forward queue
//  * command polling (leg side, demo mode, baseline reset) written by the Flutter app
// Runs in its own FreeRTOS task on core 0; sensing/analysis/OLED stay on core 1 and never wait on the network.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"
#include "secrets.h"
#include "storage.h"

struct CloudItem { char method[8]; char path[64]; char body[512]; };

class Cloud {
 public:
  volatile char cmdLeg = 0;
  volatile int8_t cmdDemo = -1;          // -1 none, 0 off, 1 on
  volatile bool cmdResetBaseline = false;

  void begin() {
    inQ_ = xQueueCreate(6, sizeof(CloudItem));
    liveMtx_ = xSemaphoreCreateMutex();
    queue_.begin();
    prefs_.begin("diabcare", false);
    cmdSeq_ = prefs_.getULong("cmdseq", 0);
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(WIFI_SSID, WIFI_PASS);
    tls_.setInsecure();   // prototype: no server verification. Pin the Google root CA for deployment.
    xTaskCreatePinnedToCore([](void* p) { ((Cloud*)p)->run(); }, "cloud", 12288, this, 1, nullptr, 0);
  }

  // Durable records (bout / daily / delete). Safe to call from the main loop; never blocks.
  bool send(const char* method, const char* path, const char* body) {
    CloudItem it{}; strlcpy(it.method, method, sizeof it.method); strlcpy(it.path, path, sizeof it.path); strlcpy(it.body, body, sizeof it.body);
    return xQueueSend(inQ_, &it, 0) == pdTRUE;
  }
  // Best-effort live status; the newest one wins.
  void setLive(const char* liveJson, const char* indexJson, bool walking) {
    xSemaphoreTake(liveMtx_, portMAX_DELAY);
    strlcpy(live_, liveJson, sizeof live_); strlcpy(index_, indexJson, sizeof index_); walking_ = walking; liveNew_ = true;
    xSemaphoreGive(liveMtx_);
  }
  bool wifiUp() const { return WiFi.status() == WL_CONNECTED; }
  uint32_t queued() { return queue_.size(); }
  static uint32_t epoch() { time_t t = time(nullptr); return t > 1700000000 ? (uint32_t)t : 0; }

 private:
  String url(const String& path) { return String(FIREBASE_DB_URL) + path + ".json?auth=" + token_; }

  int http(const char* method, const String& fullUrl, const char* body, String* resp = nullptr) {
    HTTPClient h; h.setTimeout(8000);
    if (!h.begin(tls_, fullUrl)) return -1;
    h.addHeader("Content-Type", "application/json");
    int code = h.sendRequest(method, (uint8_t*)body, body ? strlen(body) : 0);
    if (resp && code > 0) *resp = h.getString();
    h.end();
    if (code == 401) token_ = "";            // expired -> sign in again
    return code;
  }

  bool signIn() {
    HTTPClient h; h.setTimeout(8000);
    String u = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY;
    if (!h.begin(tls_, u)) return false;
    h.addHeader("Content-Type", "application/json");
    String body = String("{\"email\":\"") + DEVICE_EMAIL + "\",\"password\":\"" + DEVICE_PASSWORD + "\",\"returnSecureToken\":true}";
    int code = h.POST(body); String resp = code > 0 ? h.getString() : ""; h.end();
    if (code != 200) return false;
    JsonDocument d; if (deserializeJson(d, resp)) return false;
    token_ = d["idToken"].as<String>();
    uint32_t ttl = (uint32_t)atol(d["expiresIn"] | "3600");      // Firebase returns it as a string
    tokenExpiry_ = millis() + (ttl > 600 ? ttl - 300 : 300) * 1000UL;
    return token_.length() > 20;
  }

  void run() {
    for (;;) { service(); vTaskDelay(pdMS_TO_TICKS(20)); }
  }

  void service() {
    CloudItem it;
    while (xQueueReceive(inQ_, &it, 0) == pdTRUE) queue_.push(it.method, it.path, it.body);   // persist first
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) return;
    if (!ntp_) { configTime(0, 0, "pool.ntp.org", "time.google.com"); ntp_ = true; }   // epoch stays UTC; local day = epoch + TZ_OFFSET_S
    if (token_.length() == 0 || (int32_t)(now - tokenExpiry_) > 0) {
      if (now - lastAuth_ < 15000 && lastAuth_) return;
      lastAuth_ = now; if (!signIn()) return;
    }

    // 1) drain the store-and-forward queue, oldest first
    for (int i = 0; i < 3; i++) {
      String m, p, b;
      if (!queue_.peek(m, p, b)) break;
      int code = http(m.c_str(), url(p), b.length() ? b.c_str() : nullptr);
      if (code >= 200 && code < 300) queue_.pop();
      else if (code >= 400 && code < 500 && code != 401 && code != 408 && code != 429) queue_.pop();   // poison record
      else break;                                                                                         // offline: retry later
    }

    // 2) live status
    bool fresh = false; static char lv[640], ix[160]; bool walking;
    xSemaphoreTake(liveMtx_, portMAX_DELAY);
    if (liveNew_ && now - lastLive_ >= (walking_ ? LIVE_PERIOD_WALK_MS : LIVE_PERIOD_IDLE_MS)) {
      strlcpy(lv, live_, sizeof lv); strlcpy(ix, index_, sizeof ix); liveNew_ = false; fresh = true;
    }
    walking = walking_;
    xSemaphoreGive(liveMtx_);
    if (fresh) {
      lastLive_ = now;
      http("PUT", url(String("/patients/") + PATIENT_ID + "/live"), lv);
      http("PUT", url(String("/index/") + PATIENT_ID), ix);
    }
    (void)walking;

    // 3) commands from the app
    if (now - lastCmd_ >= CMD_POLL_MS) {
      lastCmd_ = now; String r;
      if (http("GET", url(String("/patients/") + PATIENT_ID + "/cmd"), nullptr, &r) == 200 && r.length() > 4) {
        JsonDocument d;
        if (!deserializeJson(d, r)) {
          uint32_t seq = d["seq"] | 0u;
          if (seq > cmdSeq_) {
            cmdSeq_ = seq; prefs_.putULong("cmdseq", seq);
            const char* leg = d["leg"] | "";
            if (leg[0] == 'L' || leg[0] == 'R') cmdLeg = leg[0];
            if (!d["demo"].isNull()) cmdDemo = d["demo"].as<bool>() ? 1 : 0;
            if (d["reset"] | false) cmdResetBaseline = true;
          }
        }
      }
    }
  }

  WiFiClientSecure tls_;
  UploadQueue queue_;
  Preferences prefs_;
  QueueHandle_t inQ_ = nullptr;
  SemaphoreHandle_t liveMtx_ = nullptr;
  String token_; uint32_t tokenExpiry_ = 0, lastAuth_ = 0, lastLive_ = 0, lastCmd_ = 0, cmdSeq_ = 0;
  char live_[640] = "", index_[160] = ""; bool liveNew_ = false, walking_ = false, ntp_ = false;
};
