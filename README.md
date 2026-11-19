# DiabCare

MYOSA-based ankle-worn wearable for **diabetic mobility monitoring and foot-risk awareness**.
Indicator-based awareness tool - **not a medical device; no plantar pressure, no skin temperature, no diagnosis.**

```
MPU6050 (ankle) --I2C--> MYOSA ESP32 --> OLED (local feedback, works offline)
                            | Wi-Fi (station: clinic / home / phone hotspot)
                            +--> Firebase Realtime Database (per-device auth, flash store-and-forward)
                                      +--> Flutter app: patient view + clinician view
```
Core 1 senses/analyses at 100 Hz; core 0 runs the network task, so uploads never disturb sampling.

## Layout
| Path | What |
|---|---|
| `firmware/DiabCare/` | Main sketch: gait engine, personal-baseline analysis, follow-up level, OLED, Firebase client |
| `firmware/DataCollection/` | Streams ax,ay,az (g) @100 Hz for Edge Impulse |
| `flutter_app/` | Patient + clinician app (Firebase Auth + Realtime Database) |
| `firebase/database.rules.json` | Security rules (role map: devices / patientUsers / clinicians) |
| `tests/test_logic.cpp` | Host tests: step detection, bouts, symmetry, Mahalanobis, baseline, Demo timeline |
| `tools/simulate_patient.py` | Fake device writing the Demo layout to Firebase (test the app without hardware) |
| `docs/` | `FIREBASE_SETUP.md`, `IMPLEMENTATION_NOTES.md` (paper -> code map + open decisions) |

## Quick start
1. **Firebase:** follow `docs/FIREBASE_SETUP.md` (rules, users, access map).
2. **Firmware** (Arduino IDE, ESP32 core 2.x/3.x): libraries `Adafruit SSD1306`, `Adafruit GFX`, `ArduinoJson` (v7). Copy `secrets.example.h` -> `secrets.h`. **Verify `GYRO_SAGITTAL_AXIS` / `GYRO_SIGN`** in `config.h` on your ankle mount, then upload `firmware/DiabCare`.
   Serial (115200): `l`/`r` = instrumented leg, `d` = Demo Mode. For bench tests set `BASELINE_DAYS` to 1-2.
3. **App:** `cd flutter_app && flutter create . && flutterfire configure && flutter pub get && flutter run`.
4. **No hardware?** `python tools/simulate_patient.py ...` then open the app as clinician.
5. **TinyML:** record static / walking / other with `DataCollection` (+ `edge-impulse-data-forwarder --frequency 100`), 2 s window / 1 s stride, flatten block, int8, export Arduino library named `DiabCare_inferencing`, then uncomment `USE_EDGE_IMPULSE`. Split train/test **by session**.
6. **Tests:** `g++ -std=c++17 -I firmware/DiabCare tests/test_logic.cpp -o t && ./t`

## Behaviour
* **OLED:** activity, steps, asymmetry/loading status, *Learning / Stable / Watch / Review*; Review shows "Consider clinical follow-up". Sensor Error + retry on failure.
* **Offline:** all analysis and OLED feedback run on-device. Bouts and daily summaries are queued in flash (300 records, oldest dropped) and uploaded in order when Wi-Fi returns; live status is best-effort.
* **Demo Mode** (app switch or `d`): replays 26 synthetic days on a *separate* analysis instance (never touches the real baseline) and uploads under `patients/<id>/demo/`. Labelled as synthetic on OLED, app, and data. Simulated asymmetry (heel lift) is a proof of function, not pathological gait.

## Status / honesty notes
* Pure-logic modules (gait, analysis, Demo timeline) are unit-tested on a PC and pass. The Arduino hardware paths, Firebase REST client, and Flutter app have **not** been run on a real board/device by the author of this scaffold - expect small fixes (library versions, axis/sign, I2C).
* All thresholds are provisional; calibrate from recordings before drawing conclusions. No patient data are used; clinical validity needs an ethics-approved study.
* The prototype uses `setInsecure()` TLS; pin the Google root CA before any real use. Rules in `firebase/` must be tested in the Rules Playground.
