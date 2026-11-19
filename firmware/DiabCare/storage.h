#pragma once
// LittleFS persistence: analysis state + store-and-forward upload queue (one file per record).
#include <LittleFS.h>
#include <string.h>
#include "config.h"
#include "analysis.h"

static SemaphoreHandle_t g_fsMtx = nullptr;
struct FsLock {
  FsLock() { if (g_fsMtx) xSemaphoreTake(g_fsMtx, portMAX_DELAY); }
  ~FsLock() { if (g_fsMtx) xSemaphoreGive(g_fsMtx); }
};

inline bool storageBegin() {
  g_fsMtx = xSemaphoreCreateMutex();
  FsLock l;
  if (!LittleFS.begin(true)) return false;
  LittleFS.mkdir("/q");
  return true;
}

inline bool stateSave(const AnalysisState& s) {
  FsLock l;
  File f = LittleFS.open("/state.tmp", "w");
  if (!f) return false;
  size_t n = f.write((const uint8_t*)&s, sizeof s); f.close();
  if (n != sizeof s) return false;
  LittleFS.remove("/state.bin");
  return LittleFS.rename("/state.tmp", "/state.bin");
}

inline bool stateLoad(AnalysisState& s) {
  FsLock l;
  File f = LittleFS.open("/state.bin", "r");
  if (!f || f.size() != sizeof(AnalysisState)) return false;
  AnalysisState t; size_t n = f.read((uint8_t*)&t, sizeof t); f.close();
  if (n != sizeof t || t.magic != STATE_MAGIC) return false;
  s = t; return true;
}

// Records: line1 = HTTP method, line2 = database path, line3 = JSON body.
class UploadQueue {
 public:
  void begin() {
    FsLock l;
    uint32_t lo = 0xFFFFFFFF, hi = 0; bool any = false;
    File dir = LittleFS.open("/q"); File f = dir.openNextFile();
    while (f) {
      const char* nm = f.name(); const char* sl = strrchr(nm, '/'); uint32_t id = (uint32_t)atol(sl ? sl + 1 : nm);
      if (id) { any = true; if (id < lo) lo = id; if (id > hi) hi = id; }
      f = dir.openNextFile();
    }
    head_ = any ? lo : 1; tail_ = any ? hi + 1 : 1;
  }
  uint32_t size() const { return tail_ - head_; }

  void push(const char* method, const char* path, const char* body) {
    FsLock l;
    if (tail_ - head_ >= QUEUE_MAX) { LittleFS.remove(name(head_)); head_++; }   // drop oldest when full
    File f = LittleFS.open(name(tail_), "w");
    if (!f) return;
    f.print(method); f.print('\n'); f.print(path); f.print('\n'); f.print(body); f.close();
    tail_++;
  }

  bool peek(String& method, String& path, String& body) {
    FsLock l;
    while (head_ < tail_) {
      File f = LittleFS.open(name(head_), "r");
      if (!f) { head_++; continue; }
      method = f.readStringUntil('\n'); path = f.readStringUntil('\n'); body = f.readString(); f.close();
      return true;
    }
    return false;
  }
  void pop() { FsLock l; if (head_ < tail_) { LittleFS.remove(name(head_)); head_++; } }

 private:
  static const char* name(uint32_t id) { static char b[24]; snprintf(b, sizeof b, "/q/%lu", (unsigned long)id); return b; }
  uint32_t head_ = 1, tail_ = 1;
};
