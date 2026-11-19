# Paper -> code map, and decisions the paper left open

| Paper | Code |
|---|---|
| 100 Hz, DLPF 44 Hz, 2 s windows N=200, 50 % overlap, RMS/variance (Eq. 1) | `config.h`, `sensors.h` (DLPF_CFG=3), `signal_processing.h` |
| TinyML Static/Walking/Other + threshold fallback, 3-window confirmation | `classifier.h` |
| Step detection (swing peak, 0.3 s min interval, prominence), heel strike, loading-rate proxy, stride regularity, bouts (>=10 steps, 2 s gap) | `gait.h` |
| Symmetry index (Eq. 2), Mahalanobis (Eq. 3), CLI, z-scores, Table 3 levels, 3-cycle confirmation, weekly LS trend | `analysis.h` |
| Demo Mode replaying synthetic days | `demo_data.h`, `DiabCare.ino` (`startDemo`) |
| Firebase upload, per-device auth, ring buffer in flash, store-and-forward | `cloud.h`, `storage.h`, `firebase/database.rules.json` |
| Patient / clinician views, baseline comparison, trend graphs | `flutter_app/lib` |

## Values the paper left blank (all PROVISIONAL, in `config.h`)
swing-peak minimum and **prominence** (`PEAK_*`), **asymmetry limit** (`SI_LIMIT_PCT` 10 %), **Mahalanobis limit** (`DM_LIMIT` 4.0), RMS static floor, 1 s gravity span.

## Interpretations you should state in the paper
1. **Steps vs strides.** With one ankle sensor each swing peak is one *stride of the instrumented leg*. The code counts each detection as 2 steps (`STEPS_PER_DETECTION`), assumes alternating legs, and computes stride time from consecutive swings of the same leg. Say this explicitly (the paper currently says "steps").
2. **Active flag.** An indicator is *active* if it was set on >= 2 of the last 7 days (`ACTIVE_MIN_DAYS`); "persists for 7 days" = set on all 7. Seven indicators exist: 5 asymmetry features, loading pattern, walking behavior.
3. **Evaluation cycle = one day.** Level changes need 3 consecutive daily evaluations, so a real level change takes >= 3 days. Demo Mode compresses days.
4. **Baseline** = first 7 days *and* >= 20 bouts (needed for a stable 4x4 covariance; a small ridge is added). During the baseline no flags are raised ("Learning" on OLED/app).
5. **Asymmetry** compares the running mean of the latest L-session and R-session (>= 3 bouts each); a flag needs SI > limit **and** > baseline SI mean + 2 sd.
6. **Weekly trend** needs >= 2 weeks of history; "worsening" = slope >= 0.5 flags/week and last-week mean >= 1.
7. **Stride regularity** is the Pearson correlation of two consecutive stride segments of |a| (a streaming approximation of the autocorrelation at stride lag).
8. **Day boundary** needs NTP; without time sync the current day simply continues (no summaries are cut).
9. Accelerometer range is +-8 g (heel strikes exceed +-4 g); heel-strike and loading-rate values are relative, per-patient/per-device only.
