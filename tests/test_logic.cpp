// Host-side unit tests for gait detection and the personal-baseline analysis.
// Build: g++ -std=c++17 -Wall -I firmware/DiabCare tests/test_logic.cpp -o test_logic && ./test_logic
#include <cstdio>
#include <cmath>
#include "gait.h"
#include "analysis.h"
#include "demo_data.h"

static int fails = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

static void synthGait(GaitEngine& g, float strideS, float seconds, int& tick, float hsG = 1.2f) {
  int n = (int)(seconds * IMU_HZ);
  for (int i = 0; i < n; i++, tick++) {
    float t = tick / (float)IMU_HZ, ph = fmodf(t, strideS) / strideS;
    float w = ph < 0.5f ? 300.0f * sinf(2 * M_PI * ph) : -40.0f * sinf(2 * M_PI * (ph - 0.5f));
    float d = (ph - 0.40f) * strideS / 0.022f;                   // heel-strike impulse, ~165 ms after swing peak
    float dyn = hsG * expf(-0.5f * d * d) + 0.05f * sinf(2 * M_PI * 3 * t);
    g.push(w, dyn, 1.0f + dyn);
  }
}
static void idle(GaitEngine& g, float seconds, int& tick) {
  for (int i = 0; i < (int)(seconds * IMU_HZ); i++, tick++) g.push(0, 0.01f, 1.0f);
}

static BoutFeatures mkBout(char leg, float stride, float cv, float reg, float ahs, float lr) {
  BoutFeatures b{}; b.leg = leg; b.steps = 60; b.durS = 30; b.cadence = 110;
  b.strideMean = stride; b.strideCV = cv; b.regularity = reg; b.ahs = ahs; b.loadRate = lr; b.cli = ahs * 30;
  return b;
}

int main() {
  // ---- gait engine on a synthetic walk -------------------------------------------
  GaitEngine g; int tick = 0; BoutFeatures b{};
  synthGait(g, 1.1f, 40, tick); idle(g, 4, tick);
  CHECK(g.takeBout(b));
  printf("bout: steps=%d cad=%.1f stride=%.3f cv=%.2f reg=%.3f ahs=%.2f lr=%.1f\n",
         b.steps, b.cadence, b.strideMean, b.strideCV, b.regularity, b.ahs, b.loadRate);
  CHECK(b.steps >= 66 && b.steps <= 76);
  CHECK(b.cadence > 105 && b.cadence < 113);
  CHECK(fabsf(b.strideMean - 1.1f) < 0.02f && b.strideCV < 1.5f);
  CHECK(b.regularity > 0.9f);
  CHECK(b.ahs > 1.0f && b.ahs < 1.4f && b.loadRate > 10);
  CHECK(!g.takeBout(b));

  // short burst (< 10 steps) must not become a bout; a 3 s pause splits bouts
  GaitEngine g2; tick = 0;
  synthGait(g2, 1.1f, 4, tick); idle(g2, 4, tick);
  CHECK(!g2.takeBout(b));
  int bouts = 0;
  synthGait(g2, 1.1f, 15, tick); idle(g2, 3, tick); while (g2.takeBout(b)) bouts++;   // firmware drains every loop
  synthGait(g2, 1.1f, 15, tick); idle(g2, 3, tick); while (g2.takeBout(b)) bouts++;
  CHECK(bouts == 2);

  // standing still gives no steps
  GaitEngine g3; tick = 0; idle(g3, 20, tick); CHECK(!g3.takeBout(b) && g3.pendingSteps() == 0);

  // ---- symmetry index / Mahalanobis -------------------------------------------------
  CHECK(fabs(symmetryIndex(1.0, 1.0)) < 1e-9);
  CHECK(fabs(symmetryIndex(1.1, 0.9) - 20.0) < 1e-6);
  MvStats mv; memset(&mv, 0, sizeof mv); uint32_t st = 7;
  for (int i = 0; i < 400; i++) { double x[FD]; for (int k = 0; k < FD; k++) x[k] = 1.7320508 * demoNoise(st); mv.add(x); }
  double far4[FD] = {5, 0, 0, 0}, near0[FD] = {0, 0, 0, 0};
  double dFar = mahalanobis(mv, far4), dNear = mahalanobis(mv, near0);
  printf("DM far=%.2f near=%.2f\n", dFar, dNear);
  CHECK(dFar > 4.3 && dFar < 5.7 && dNear < 0.3);

  // ---- engine: bout-level loading + symmetry ----------------------------------------
  AnalysisEngine e; e.minBoutsForBaseline = 20;
  e.setLeg('L');
  uint32_t r = 99;
  for (int i = 0; i < 30; i++) {
    e.setLeg(i % 2 ? 'R' : 'L');
    e.onBout(mkBout(e.s.curLeg, 1.10f + 0.02f * demoNoise(r), 3 + 0.3f * demoNoise(r), 0.9f + 0.02f * demoNoise(r),
                    1.2f + 0.05f * demoNoise(r), 40 + 2 * demoNoise(r)));
  }
  for (int d = 1; d <= BASELINE_DAYS; d++) { DailySummary x; demoSummary(d, x); e.processDay(x); }
  CHECK(e.s.baseDone);
  e.setLeg('L');
  BoutVerdict v = e.onBout(mkBout('L', 1.10f, 3.0f, 0.9f, 1.2f, 40));
  CHECK(!v.abnormal);
  v = e.onBout(mkBout('L', 1.10f, 9.0f, 0.5f, 2.5f, 90));          // clearly different bout
  printf("abnormal-bout DM=%.2f\n", v.dm);
  CHECK(v.abnormal);

  // ---- Demo Mode: Stable -> Watch -> Review -----------------------------------------
  AnalysisEngine de; de.minBoutsForBaseline = 0;
  int firstWatch = 0, firstReview = 0; bool early = false;
  for (int d = 1; d <= DEMO_DAYS; d++) {
    DailySummary x; demoSummary(d, x); DailySummary o = de.processDay(x);
    printf("day %2d mask=0x%02x risk=%s active=%d\n", d, o.mask, levelName(o.risk), de.activeFlags());
    if (d <= 13 && o.risk != LEVEL_STABLE) early = true;
    if (!firstWatch && o.risk == LEVEL_WATCH) firstWatch = d;
    if (!firstReview && o.risk == LEVEL_REVIEW) firstReview = d;
  }
  printf("first Watch day %d, first Review day %d\n", firstWatch, firstReview);
  CHECK(!early && firstWatch > 0 && firstReview > firstWatch && firstReview <= DEMO_DAYS);

  // level changes need 3 consecutive cycles
  AnalysisEngine h; h.minBoutsForBaseline = 0;
  for (int d = 1; d <= 7; d++) { DailySummary x; demoSummary(d, x); h.processDay(x); }
  DailySummary bad; demoSummary(25, bad); bad.sym[0] = bad.sym[1] = bad.sym[2] = 30;
  h.processDay(bad); h.processDay(bad);
  CHECK(h.s.level == LEVEL_STABLE);

  // persistence round-trip
  AnalysisState copy; memcpy(&copy, &de.s, sizeof copy);
  CHECK(copy.magic == STATE_MAGIC && copy.level == de.s.level && copy.histN == de.s.histN);

  printf(fails ? "\n%d FAILED\n" : "\nAll tests passed\n", fails);
  return fails;
}
