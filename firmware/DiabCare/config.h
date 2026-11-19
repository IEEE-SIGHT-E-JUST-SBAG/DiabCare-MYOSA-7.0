#pragma once
// ============================================================================
// DiabCare - central configuration. Values marked PROVISIONAL are design values
// to be replaced by numbers derived from the team's recordings (paper Sec. 4).
// ============================================================================
#define FW_VERSION "1.0.0"

// ---- MYOSA ESP32 motherboard -------------------------------------------------
#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_CLOCK 400000          // reduce to 100000 if the JST-cable (shin-mounted) setup drops out
#define OLED_ADDR 0x3C
#define MPU_ADDR 0x68

// ---- Sampling / windows (paper: 100 Hz, DLPF 44 Hz, 2 s windows, 50 % overlap)
#define IMU_HZ 100
#define IMU_PERIOD_US 10000UL
#define WINDOW_N 200
#define WINDOW_STRIDE 100
#define MA_N 100                  // 1 s moving average for gravity removal

// ---- Mounting (lateral ankle). VERIFY on the assembled prototype -------------
// Sagittal-plane rotation is about the medio-lateral axis = the axis normal to the
// board face that touches the ankle. 0=x 1=y 2=z. Flip GYRO_SIGN if swing peaks are negative.
#define GYRO_SAGITTAL_AXIS 2
#define GYRO_SIGN 1
#define DEFAULT_LEG 'L'           // instrumented leg for this session ('L' or 'R'); changeable from the app

// ---- Step detection (PROVISIONAL) --------------------------------------------
#define PEAK_MIN_DPS 80.0f        // minimum swing peak of sagittal angular velocity
#define PEAK_PROM_DPS 60.0f       // minimum prominence (peak - preceding trough)
#define MIN_STEP_INTERVAL_MS 300  // paper value
#define HS_WINDOW_MS 250          // heel-strike search window after the swing peak
#define BOUT_MIN_STEPS 10         // paper: >= 10 steps without a pause > 2 s
#define BOUT_MAX_GAP_MS 2000
// One ankle sensor sees one swing per stride of the instrumented leg. Each detection
// therefore counts as 2 steps (left+right). Set to 1 if you want detections reported raw.
#define STEPS_PER_DETECTION 2
#define MAX_BOUT_STRIDES 256

// ---- Activity (Static / Walking / Other) -------------------------------------
#define RMS_STATIC_G 0.04f        // PROVISIONAL (noise floor + 3 sigma)
#define ACTIVITY_CONFIRM_WINDOWS 3
// #define USE_EDGE_IMPULSE        // uncomment after adding the exported library
#define EI_MIN_CONFIDENCE 0.60f

// ---- Personal-baseline analysis (PROVISIONAL) --------------------------------
#define BASELINE_DAYS 7           // paper: first 7 days (use 1-2 for bench tests)
#define BASELINE_MIN_BOUTS 20     // need enough bouts for a 4x4 covariance
#define MIN_COV_BOUTS 10
#define MIN_LEG_BOUTS 3           // bouts per leg session before a symmetry index is computed
#define SI_LIMIT_PCT 10.0f        // asymmetry limit (paper leaves it blank)
#define SI_BASE_SD_K 2.0f         // also must exceed baseline SI mean + k*sd
#define DM_LIMIT 4.0f             // Mahalanobis limit (~ chi-square 4 dof, 99 %)
#define LOAD_FLAG_FRACTION 0.30f  // fraction of abnormal bouts that sets the daily loading flag
#define LOAD_MIN_BOUTS 3
#define Z_LIMIT 2.0f              // paper: |z| >= 2 ...
#define Z_CONSEC_DAYS 2           // ... on at least 2 consecutive days
#define SIGMA_FLOOR_FRAC 0.10f    // baseline sigma floor = 10 % of baseline mean
#define ACTIVE_MIN_DAYS 2         // a flag is "active" if set on >= 2 of the last 7 days
#define WEEK_DAYS 7
#define TREND_SLOPE_MIN 0.5f      // flags/week
#define TREND_MIN_LAST_WEEK 1.0f
#define RISK_CONFIRM_CYCLES 3     // evaluation cycles (days) before a level change is applied
#define HIST_DAYS 28

// ---- Time / cloud ---------------------------------------------------------------
#define TZ_OFFSET_S 7200          // local day boundary (Egypt standard time); adjust for DST
#define LIVE_PERIOD_WALK_MS 5000UL
#define LIVE_PERIOD_IDLE_MS 30000UL
#define CMD_POLL_MS 20000UL
#define QUEUE_MAX 300             // store-and-forward ring (records in flash)
#define PERSIST_PERIOD_MS 600000UL

// ---- Demo mode -----------------------------------------------------------------------
#define DEMO_DAY_PERIOD_MS 2000UL // one synthetic day per 2 s
