#pragma once
// Pure C++ streaming gait engine: swing-peak step detection on sagittal angular
// velocity, heel-strike peak + loading-rate proxy, stride regularity, bout
// segmentation and per-bout features (paper Sec. 5, "Step Detection and Gait Features").
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "types.h"

class GaitEngine {
 public:
  GaitEngine() { reset(); }

  void reset() {
    n_ = 0; have_ = false; pend_ = false; cnt_ = 0; ready_ = false;
    wPrev_ = wPrev2_ = 0; prevDyn_ = 0; trough_ = 1e9f; nRecent_ = 0;
    memset(recent_, 0, sizeof recent_);
  }

  // One sample @100 Hz. wS: sagittal angular velocity (deg/s, sign-corrected).
  // dyn: gravity-removed acceleration magnitude (g). mag: raw |a| (g).
  void push(float wS, float dyn, float mag) {
    magBuf_[n_ & (BUF - 1)] = mag;

    if (pend_) {                                    // heel-strike scan after a swing peak
      float slope = (dyn - prevDyn_) * IMU_HZ;
      if (slope > slopeMax_) slopeMax_ = slope;
      if (dyn > best_) { best_ = dyn; bestSlope_ = slopeMax_; }
      if (n_ >= pendEnd_) finishStride();
    }

    if (n_ >= 2 && wPrev_ > wPrev2_ && wPrev_ >= wS && wPrev_ >= PEAK_MIN_DPS &&
        (wPrev_ - trough_) >= PEAK_PROM_DPS) {
      uint32_t pk = n_ - 1;
      if (!have_ || (pk - lastPk_) * 1000UL / IMU_HZ >= MIN_STEP_INTERVAL_MS) accept(pk);
    }
    if (wS < trough_) trough_ = wS;

    wPrev2_ = wPrev_; wPrev_ = wS; prevDyn_ = dyn; n_++;

    if (cnt_ && (n_ - evs_[cnt_ - 1].idx) * 1000UL / IMU_HZ > BOUT_MAX_GAP_MS + HS_WINDOW_MS)
      finalizeBout();
  }

  bool takeBout(BoutFeatures& out) { if (!ready_) return false; out = bout_; ready_ = false; return true; }
  uint32_t pendingSteps() const { return (uint32_t)cnt_ * STEPS_PER_DETECTION; }

  // detections within the last 3 s (used by the threshold activity fallback)
  int recentStrides() const {
    int c = 0;
    for (int i = 0; i < 4; i++) if (i < nRecent_ && (n_ - recent_[i]) < 3UL * IMU_HZ) c++;
    return c;
  }
  char leg = 'L';   // leg label stamped on bouts (set by the caller)

 private:
  static const int BUF = 512;  // power of two
  struct Ev { uint32_t idx; float ahs, lr, reg; };

  float get(uint32_t i) const { return magBuf_[i & (BUF - 1)]; }

  // Pearson correlation between two consecutive stride segments of |a| (stride regularity).
  float regularity(uint32_t pk, uint32_t T) const {
    if (T < 30 || 2 * T + 2 > (uint32_t)BUF || pk < 2 * T) return NAN;
    uint32_t a0 = pk - 2 * T, b0 = pk - T;
    double ma = 0, mb = 0;
    for (uint32_t i = 0; i < T; i++) { ma += get(a0 + i); mb += get(b0 + i); }
    ma /= T; mb /= T;
    double sab = 0, saa = 0, sbb = 0;
    for (uint32_t i = 0; i < T; i++) {
      double a = get(a0 + i) - ma, b = get(b0 + i) - mb;
      sab += a * b; saa += a * a; sbb += b * b;
    }
    double den = sqrt(saa * sbb);
    return den > 1e-9 ? (float)(sab / den) : NAN;
  }

  void accept(uint32_t pk) {
    if (pend_) finishStride();
    pendReg_ = have_ ? regularity(pk, pk - lastPk_) : NAN;
    pendPk_ = pk; lastPk_ = pk; have_ = true;
    trough_ = 1e9f;
    pend_ = true; pendEnd_ = pk + (uint32_t)(HS_WINDOW_MS * IMU_HZ / 1000);
    best_ = 0; bestSlope_ = 0; slopeMax_ = -1e9f;
    if (nRecent_ < 4) recent_[nRecent_++] = pk;
    else { memmove(recent_, recent_ + 1, 3 * sizeof(uint32_t)); recent_[3] = pk; }
  }

  void finishStride() {
    pend_ = false;
    Ev e{pendPk_, best_, bestSlope_ > 0 ? bestSlope_ : 0, pendReg_};
    if (cnt_ && (e.idx - evs_[cnt_ - 1].idx) * 1000UL / IMU_HZ > BOUT_MAX_GAP_MS) finalizeBout();
    if (cnt_ == MAX_BOUT_STRIDES) finalizeBout();
    evs_[cnt_++] = e;
  }

  void finalizeBout() {
    int n = cnt_; cnt_ = 0;
    int steps = n * STEPS_PER_DETECTION;
    if (steps < BOUT_MIN_STEPS || n < 3) return;
    double sum = 0, sq = 0; int m = n - 1;
    for (int i = 1; i < n; i++) { double s = (evs_[i].idx - evs_[i - 1].idx) / (double)IMU_HZ; sum += s; sq += s * s; }
    double mean = sum / m, var = m > 1 ? (sq - m * mean * mean) / (m - 1) : 0;
    if (var < 0) var = 0;
    double reg = 0; int rn = 0, ahsN = 0; double ahs = 0, lr = 0, cli = 0;
    for (int i = 0; i < n; i++) {
      if (i > 0 && !isnan(evs_[i].reg)) { reg += evs_[i].reg; rn++; }
      ahs += evs_[i].ahs; lr += evs_[i].lr; cli += evs_[i].ahs; ahsN++;
    }
    double dur = (evs_[n - 1].idx - evs_[0].idx) / (double)IMU_HZ + mean;
    bout_.leg = leg; bout_.steps = (uint16_t)steps; bout_.durS = (float)dur;
    bout_.cadence = (float)(steps * 60.0 / dur);
    bout_.strideMean = (float)mean; bout_.strideCV = (float)(mean > 0 ? 100.0 * sqrt(var) / mean : 0);
    bout_.regularity = rn ? (float)(reg / rn) : 0;
    bout_.ahs = (float)(ahs / ahsN); bout_.loadRate = (float)(lr / ahsN); bout_.cli = (float)cli;
    ready_ = true;
  }

  float magBuf_[BUF];
  Ev evs_[MAX_BOUT_STRIDES];
  int cnt_;
  uint32_t n_, lastPk_ = 0, pendPk_ = 0, pendEnd_ = 0, recent_[4];
  int nRecent_;
  bool have_, pend_, ready_;
  float wPrev_, wPrev2_, prevDyn_, trough_, best_ = 0, bestSlope_ = 0, slopeMax_ = 0, pendReg_ = NAN;
  BoutFeatures bout_;
};
