#pragma once
// Activity classifier (Static / Walking / Other): Edge Impulse TinyML + threshold fallback.
#include <string.h>
#include "config.h"
#include "types.h"
#ifdef USE_EDGE_IMPULSE
#include <DiabCare_inferencing.h>
#endif

struct ClassResult { ActivityClass cls; float conf; bool usedModel; };

// Fallback: RMS floor for Static; otherwise Walking only if the gait engine saw >= 2 swings in 3 s.
inline ActivityClass classifyThreshold(float rms, int recentStrides) {
  if (rms < RMS_STATIC_G) return ACT_STATIC;
  return recentStrides >= 2 ? ACT_WALKING : ACT_OTHER;
}

#ifdef USE_EDGE_IMPULSE
static const float* g_eiInput = nullptr;
static int eiGetData(size_t o, size_t l, float* out) { memcpy(out, g_eiInput + o, l * sizeof(float)); return 0; }
static_assert(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE == WINDOW_N * 3, "EI window: 2 s @ 100 Hz, ax,ay,az in g");
#endif

inline ClassResult classifyWindow(const float* interleaved, float rms, int recentStrides) {
  ClassResult r{classifyThreshold(rms, recentStrides), 1.0f, false};
#ifdef USE_EDGE_IMPULSE
  g_eiInput = interleaved;
  signal_t sig; sig.total_length = WINDOW_N * 3; sig.get_data = &eiGetData;
  ei_impulse_result_t res;
  if (run_classifier(&sig, &res, false) == EI_IMPULSE_OK) {
    int best = 0;
    for (size_t i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++)
      if (res.classification[i].value > res.classification[best].value) best = i;
    float c = res.classification[best].value; const char* lb = res.classification[best].label;
    if (c >= EI_MIN_CONFIDENCE) {
      if (!strcmp(lb, "static")) r = {ACT_STATIC, c, true};
      else if (!strcmp(lb, "walking")) r = {ACT_WALKING, c, true};
      else if (!strcmp(lb, "other")) r = {ACT_OTHER, c, true};
    }
  }
#endif
  return r;
}

// Accept a class change only after N consecutive windows.
class ActivityConfirm {
 public:
  ActivityClass confirmed = ACT_STATIC;
  void onWindow(ActivityClass raw) {
    if (raw == cand_) cnt_++; else { cand_ = raw; cnt_ = 1; }
    if (cnt_ >= ACTIVITY_CONFIRM_WINDOWS) confirmed = cand_;
  }
 private:
  ActivityClass cand_ = ACT_STATIC; int cnt_ = 0;
};
