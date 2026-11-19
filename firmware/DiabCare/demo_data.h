#pragma once
// Synthetic daily summaries for Demo Mode (clearly labelled as synthetic everywhere).
// Days 1-7: baseline. Day 14+: walking-behaviour decline. Day 17+: abnormal loading.
// Day 20+: gait asymmetry. Drives Stable -> Watch -> Review.
#include "analysis.h"

constexpr int DEMO_DAYS = 26;

inline float demoNoise(uint32_t& st) {
  st = st * 1664525u + 1013904223u;
  return ((st >> 8) & 0xFFFF) / 65535.0f * 2.0f - 1.0f;   // [-1, 1]
}

inline void demoSummary(int day, DailySummary& d) {
  uint32_t st = 12345u + (uint32_t)day * 977u;
  memset(&d, 0, sizeof d);
  d.day = day;
  bool decline = day >= 14;
  d.met[0] = 6000.0f * (decline ? 0.65f : 1.0f) * (1 + 0.04f * demoNoise(st));
  d.met[1] = 70.0f * (decline ? 0.65f : 1.0f) * (1 + 0.05f * demoNoise(st));
  d.met[2] = 40.0f * (1 + 0.07f * demoNoise(st));
  d.met[3] = 60.0f * (1 + 0.06f * demoNoise(st));
  d.met[4] = 100.0f * (1 + 0.02f * demoNoise(st));
  d.met[5] = 55.0f * (decline ? 1.6f : 1.0f) * (1 + 0.08f * demoNoise(st));
  d.cli = 2000.0f;
  d.abnFrac = day >= 17 ? 0.40f : 0.03f;
  for (int i = 0; i < NSYM; i++) d.sym[i] = 4.0f + 1.0f * demoNoise(st);
  if (day >= 20) { d.sym[1] = 18.0f; d.sym[3] = 18.0f; d.sym[4] = 18.0f; }
}
