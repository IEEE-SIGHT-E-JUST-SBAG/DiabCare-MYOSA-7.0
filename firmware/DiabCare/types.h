#pragma once
#include <stdint.h>
enum ActivityClass { ACT_STATIC = 0, ACT_WALKING = 1, ACT_OTHER = 2 };
enum FollowLevel { LEVEL_STABLE = 0, LEVEL_WATCH = 1, LEVEL_REVIEW = 2 };

inline const char* actName(ActivityClass a) { return a == ACT_STATIC ? "Static" : a == ACT_WALKING ? "Walking" : "Other"; }
inline const char* levelName(int l) { return l == 0 ? "Stable" : l == 1 ? "Watch" : "Review"; }

struct BoutFeatures {
  char leg;            // 'L' / 'R'
  uint16_t steps;
  float durS, cadence;       // steps/min
  float strideMean, strideCV;  // s, %
  float regularity;          // 0..1
  float ahs;                 // mean heel-strike peak (g, relative use only)
  float loadRate;            // mean loading-rate proxy (g/s, relative use only)
  float cli;                 // sum of ahs over detected strides (cumulative loading contribution)
};
