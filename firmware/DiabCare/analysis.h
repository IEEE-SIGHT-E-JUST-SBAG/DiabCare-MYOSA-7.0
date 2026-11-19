#pragma once
// Pure C++ personal-baseline analysis (paper Sec. 5, "AI Analysis Layer"):
//  - gait asymmetry (symmetry index, Eq. 2)
//  - foot-loading pattern (Mahalanobis distance, Eq. 3) + cumulative loading index
//  - walking-behaviour change (z-scores, |z|>=2 on >=2 consecutive days)
//  - longitudinal follow-up indicator Stable / Watch / Review (Table 3)
// All state lives in plain structs so it can be saved to flash with memcpy.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>
#include "config.h"
#include "types.h"

constexpr int FD = 4;      // loading feature vector: strideCV, regularity, ahs, loadRate
constexpr int NSYM = 5;    // symmetry features: strideMean, strideCV, regularity, ahs, loadRate
constexpr int NMET = 6;    // daily metrics: steps, walkMin, bouts, meanBoutLen, cadence, longestStaticMin
constexpr int NFLAGS = 7;  // bits 0-4 asymmetry per feature, 5 loading pattern, 6 walking behaviour
constexpr uint32_t STATE_MAGIC = 0xD1AB0001;

struct Stats1 {
  uint32_t n; double mean, m2;
  void add(double x) { n++; double d = x - mean; mean += d / n; m2 += d * (x - mean); }
  double sd() const { return n > 1 ? sqrt(m2 / (n - 1)) : 0.0; }
};

struct MvStats {  // multivariate Welford
  uint32_t n; double mean[FD]; double m2[FD][FD];
  void add(const double x[FD]) {
    double d[FD];
    n++;
    for (int i = 0; i < FD; i++) { d[i] = x[i] - mean[i]; mean[i] += d[i] / n; }
    for (int i = 0; i < FD; i++) for (int j = 0; j < FD; j++) m2[i][j] += d[i] * (x[j] - mean[j]);
  }
};

// Mahalanobis distance of x from the baseline; returns NAN if covariance is unusable.
inline double mahalanobis(const MvStats& s, const double x[FD]) {
  if (s.n < 3) return NAN;
  double a[FD][2 * FD];
  double tr = 0;
  for (int i = 0; i < FD; i++) tr += s.m2[i][i] / (s.n - 1);
  double ridge = 1e-4 * tr / FD + 1e-9;                    // regularization for few bouts
  for (int i = 0; i < FD; i++) for (int j = 0; j < FD; j++) {
    a[i][j] = s.m2[i][j] / (s.n - 1) + (i == j ? ridge : 0);
    a[i][FD + j] = (i == j) ? 1.0 : 0.0;
  }
  for (int c = 0; c < FD; c++) {                           // Gauss-Jordan, partial pivoting
    int p = c;
    for (int r = c + 1; r < FD; r++) if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
    if (fabs(a[p][c]) < 1e-14) return NAN;
    if (p != c) for (int k = 0; k < 2 * FD; k++) { double t = a[c][k]; a[c][k] = a[p][k]; a[p][k] = t; }
    double pv = a[c][c];
    for (int k = 0; k < 2 * FD; k++) a[c][k] /= pv;
    for (int r = 0; r < FD; r++) if (r != c) {
      double f = a[r][c];
      for (int k = 0; k < 2 * FD; k++) a[r][k] -= f * a[c][k];
    }
  }
  double d[FD], q = 0;
  for (int i = 0; i < FD; i++) d[i] = x[i] - s.mean[i];
  for (int i = 0; i < FD; i++) for (int j = 0; j < FD; j++) q += d[i] * a[i][FD + j] * d[j];
  return q > 0 ? sqrt(q) : 0.0;
}

inline double symmetryIndex(double xl, double xr) {          // Eq. (2), percent
  double m = 0.5 * (xl + xr);
  return fabs(m) > 1e-9 ? 100.0 * fabs(xl - xr) / fabs(m) : 0.0;
}

struct DailySummary {
  uint32_t day;
  float met[NMET];
  float cli;
  float abnFrac;       // fraction of abnormal bouts, -1 = not available
  float sym[NSYM];     // latest symmetry indices (%), -1 = not available
  uint16_t mask;       // active-flag bits for this day
  uint8_t risk;        // follow-up level after this day
  uint8_t baseDone;
};

struct LegAgg { uint16_t n; double sum[NSYM]; };

struct AnalysisState {
  uint32_t magic;
  // baseline
  MvStats boutBase; Stats1 dayBase[NMET]; Stats1 symBase[NSYM];
  uint16_t baseDays; uint8_t baseDone; uint8_t zRun[NMET];
  // leg sessions + live flags
  LegAgg leg[2]; char curLeg;
  float symLive[NSYM]; uint8_t liveAsymMask; uint8_t liveLoadFlag; float lastDM;
  // today's accumulators
  uint32_t curDay, steps, bouts, abnBouts, boutSteps, walkS, staticRun, longestStatic;
  double cadW, cliSum;
  // history + indicator
  DailySummary hist[HIST_DAYS]; uint16_t histN;
  uint8_t level, pendLevel, pendCount;
};
static_assert(std::is_trivially_copyable<AnalysisState>::value, "state must be memcpy-able");

struct BoutVerdict { double dm; bool abnormal; uint8_t asymMask; };

class AnalysisEngine {
 public:
  AnalysisState s;
  uint32_t minBoutsForBaseline = BASELINE_MIN_BOUTS;   // 0 for synthetic demo data

  AnalysisEngine() { reset(); }
  void reset() {
    memset(&s, 0, sizeof s); s.magic = STATE_MAGIC; s.curLeg = DEFAULT_LEG;
    for (int i = 0; i < NSYM; i++) s.symLive[i] = -1;
    s.lastDM = NAN;
  }
  void resetBaseline() { char l = s.curLeg; reset(); s.curLeg = l; }

  void setLeg(char c) {
    if (c != 'L' && c != 'R') return;
    s.curLeg = c;
    memset(&s.leg[c == 'R'], 0, sizeof(LegAgg));          // a new session for this leg
  }

  static void vec(const BoutFeatures& b, double x[FD]) {
    x[0] = b.strideCV; x[1] = b.regularity; x[2] = b.ahs; x[3] = b.loadRate;
  }
  static void symVals(const BoutFeatures& b, double v[NSYM]) {
    v[0] = b.strideMean; v[1] = b.strideCV; v[2] = b.regularity; v[3] = b.ahs; v[4] = b.loadRate;
  }

  bool symFlag(int i, double si) const {
    if (!s.baseDone || si < 0) return false;
    if (si <= SI_LIMIT_PCT) return false;
    return s.symBase[i].n < 3 || si > s.symBase[i].mean + SI_BASE_SD_K * s.symBase[i].sd();
  }

  BoutVerdict onBout(const BoutFeatures& b) {
    BoutVerdict v{NAN, false, 0};
    LegAgg& L = s.leg[s.curLeg == 'R'];
    double sv[NSYM]; symVals(b, sv);
    L.n++; for (int i = 0; i < NSYM; i++) L.sum[i] += sv[i];

    double x[FD]; vec(b, x);
    if (!s.baseDone) s.boutBase.add(x);
    else if (s.boutBase.n >= MIN_COV_BOUTS) { v.dm = mahalanobis(s.boutBase, x); v.abnormal = !isnan(v.dm) && v.dm > DM_LIMIT; }
    s.lastDM = (float)v.dm; s.liveLoadFlag = v.abnormal;

    s.steps += b.steps; s.bouts++; s.boutSteps += b.steps;
    s.cadW += (double)b.cadence * b.steps; s.cliSum += b.cli;
    if (v.abnormal) s.abnBouts++;

    const LegAgg& A = s.leg[0]; const LegAgg& B = s.leg[1];
    s.liveAsymMask = 0;
    for (int i = 0; i < NSYM; i++) {
      if (A.n >= MIN_LEG_BOUTS && B.n >= MIN_LEG_BOUTS) {
        double si = symmetryIndex(A.sum[i] / A.n, B.sum[i] / B.n);
        s.symLive[i] = (float)si;
        if (symFlag(i, si)) s.liveAsymMask |= 1 << i;
      } else s.symLive[i] = -1;
    }
    v.asymMask = s.liveAsymMask;
    return v;
  }

  void onActivitySecond(ActivityClass a) {
    if (a == ACT_WALKING) { s.walkS++; s.staticRun = 0; }
    else if (a == ACT_STATIC) { s.staticRun++; if (s.staticRun > s.longestStatic) s.longestStatic = s.staticRun; }
    else s.staticRun = 0;
  }

  DailySummary todaySummary() const {
    DailySummary d; memset(&d, 0, sizeof d);
    d.day = s.curDay;
    d.met[0] = (float)s.steps; d.met[1] = s.walkS / 60.0f; d.met[2] = (float)s.bouts;
    d.met[3] = s.bouts ? (float)s.boutSteps / s.bouts : 0;
    d.met[4] = s.boutSteps ? (float)(s.cadW / s.boutSteps) : 0;
    d.met[5] = s.longestStatic / 60.0f;
    d.cli = (float)s.cliSum;
    d.abnFrac = s.bouts >= LOAD_MIN_BOUTS ? (float)s.abnBouts / s.bouts : -1.0f;
    for (int i = 0; i < NSYM; i++) d.sym[i] = s.symLive[i];
    return d;
  }

  // End the current day: build summary, run the analysis, reset the accumulators.
  DailySummary onDayEnd() {
    DailySummary d = processDay(todaySummary());
    s.steps = s.bouts = s.abnBouts = s.boutSteps = s.walkS = s.staticRun = s.longestStatic = 0;
    s.cadW = s.cliSum = 0;
    return d;
  }

  double sigma(int m) const {
    double sd = s.dayBase[m].sd(), fl = SIGMA_FLOOR_FRAC * fabs(s.dayBase[m].mean);
    return sd > fl ? (sd > 1e-6 ? sd : 1e-6) : (fl > 1e-6 ? fl : 1e-6);
  }

  // Core daily evaluation; also used by Demo Mode with synthetic summaries.
  DailySummary processDay(DailySummary d) {
    d.mask = 0;
    if (!s.baseDone) {
      for (int m = 0; m < NMET; m++) s.dayBase[m].add(d.met[m]);
      for (int i = 0; i < NSYM; i++) if (d.sym[i] >= 0) s.symBase[i].add(d.sym[i]);
      s.baseDays++;
      if (s.baseDays >= BASELINE_DAYS && s.boutBase.n >= minBoutsForBaseline) s.baseDone = 1;
    } else {
      for (int i = 0; i < NSYM; i++) if (d.sym[i] >= 0 && symFlag(i, d.sym[i])) d.mask |= 1 << i;
      if (d.abnFrac >= LOAD_FLAG_FRACTION) d.mask |= 1 << 5;
      bool beh = false;
      if (s.dayBase[0].n >= 3)
        for (int m = 0; m < NMET; m++) {
          double z = (d.met[m] - s.dayBase[m].mean) / sigma(m);
          s.zRun[m] = fabs(z) >= Z_LIMIT ? (uint8_t)(s.zRun[m] + 1) : 0;
          if (s.zRun[m] >= Z_CONSEC_DAYS) beh = true;
        }
      if (beh) d.mask |= 1 << 6;
    }
    d.baseDone = s.baseDone;

    if (s.histN == HIST_DAYS) { memmove(&s.hist[0], &s.hist[1], sizeof(DailySummary) * (HIST_DAYS - 1)); s.histN--; }
    s.hist[s.histN++] = d;

    if (s.baseDone) evaluateLevel();
    s.hist[s.histN - 1].risk = s.level;
    d.risk = s.level;
    return d;
  }

  static int popcount16(uint16_t v) { int c = 0; while (v) { c += v & 1; v >>= 1; } return c; }

  int activeFlags(bool* persist = nullptr) const {
    int active = 0; bool pers = false;
    int n = s.histN < WEEK_DAYS ? s.histN : WEEK_DAYS;
    for (int b = 0; b < NFLAGS; b++) {
      int c = 0;
      for (int k = 0; k < n; k++) c += (s.hist[s.histN - 1 - k].mask >> b) & 1;
      if (c >= ACTIVE_MIN_DAYS) active++;
      if (s.histN >= WEEK_DAYS && c == WEEK_DAYS) pers = true;
    }
    if (persist) *persist = pers;
    return active;
  }

  // Least-squares slope of weekly mean flag counts (flags per week); NAN if < 2 weeks.
  double weeklyTrend(double* lastMean = nullptr) const {
    int weeks = s.histN / WEEK_DAYS; if (weeks > 4) weeks = 4;
    if (weeks < 2) return NAN;
    double y[4];
    for (int w = 0; w < weeks; w++) {
      int start = s.histN - (weeks - w) * WEEK_DAYS; double sum = 0;
      for (int k = 0; k < WEEK_DAYS; k++) sum += popcount16(s.hist[start + k].mask);
      y[w] = sum / WEEK_DAYS;
    }
    double mx = (weeks - 1) / 2.0, my = 0;
    for (int w = 0; w < weeks; w++) my += y[w];
    my /= weeks;
    double num = 0, den = 0;
    for (int w = 0; w < weeks; w++) { num += (w - mx) * (y[w] - my); den += (w - mx) * (w - mx); }
    if (lastMean) *lastMean = y[weeks - 1];
    return num / den;
  }

  void evaluateLevel() {
    bool persist = false; int active = activeFlags(&persist);
    double lm = 0, slope = weeklyTrend(&lm);
    bool worsening = !isnan(slope) && slope >= TREND_SLOPE_MIN && lm >= TREND_MIN_LAST_WEEK;
    uint8_t target = (active >= 3 || persist) ? LEVEL_REVIEW : (active == 2 || worsening) ? LEVEL_WATCH : LEVEL_STABLE;
    if (target == s.level) { s.pendCount = 0; s.pendLevel = s.level; return; }
    if (target == s.pendLevel && s.pendCount > 0) s.pendCount++; else { s.pendLevel = target; s.pendCount = 1; }
    if (s.pendCount >= RISK_CONFIRM_CYCLES) { s.level = target; s.pendCount = 0; }
  }

  int baselinePct() const {
    int p = s.baseDone ? 100 : (int)(100.0 * s.baseDays / BASELINE_DAYS);
    return p > 99 && !s.baseDone ? 99 : p;
  }
};
