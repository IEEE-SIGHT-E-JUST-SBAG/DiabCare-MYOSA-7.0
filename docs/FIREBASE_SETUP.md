# Firebase setup (Realtime Database + Auth)

1. Create a Firebase project; enable **Realtime Database** and **Authentication -> Email/Password**.
2. Paste `firebase/database.rules.json` into the database *Rules* tab and publish.
3. Create three kinds of users in Authentication and note their **UIDs**:
   * the **device** account (put its e-mail/password in `firmware/DiabCare/secrets.h`),
   * a **patient** app account, a **clinician** app account.
4. In the database *Data* tab add (this is the access-control map; only you can edit it):
```json
{
  "devices":      { "<device-uid>":  "P-0001" },
  "patientUsers": { "<patient-uid>": "P-0001" },
  "clinicians":   { "<clinician-uid>": true }
}
```
5. Copy the **Web API key** and the **database URL** into `secrets.h`; run `flutterfire configure` in `flutter_app/`.

Data layout written by the device (pseudonymous patient code only - never names):
```
patients/P-0001/live            latest status (best effort, every 5 s walking / 30 s idle)
patients/P-0001/bouts/<pushId>  one record per walking bout (store-and-forward)
patients/P-0001/daily/<dayIdx>  one daily summary + baseline mean/sigma (store-and-forward)
patients/P-0001/demo/...        synthetic Demo Mode data (never mixed with real data)
patients/P-0001/cmd             written by the app: {leg:"L"|"R", demo:bool, reset:bool, seq:<unix s>}
index/P-0001                    {risk, ts, base, demo, err} for the clinician patient list
```
`dayIdx` = (UTC epoch + TZ_OFFSET_S) / 86400. `mask` bits: 0-4 asymmetry (stride time, stride variability, regularity, heel-strike peak, loading rate), 5 loading pattern, 6 walking behaviour.
Note: records are only as safe as the rules above - test them with the Rules Playground, and pin the Google root CA in firmware before any real deployment.
