#!/usr/bin/env python3
"""Fake DiabCare device: writes the same Firebase paths as the firmware (Demo Mode layout) so you can
test the Flutter app and the security rules without hardware.
  pip install requests
  python tools/simulate_patient.py --api-key AIza... --db-url https://X-default-rtdb.firebasedatabase.app \
      --email device-p0001@diabcare.example --password '...' --pid P-0001
"""
import argparse, json, random, time, requests

ap = argparse.ArgumentParser()
for a in ("api-key", "db-url", "email", "password"): ap.add_argument(f"--{a}", required=True)
ap.add_argument("--pid", default="P-0001"); ap.add_argument("--period", type=float, default=2.0)
a = ap.parse_args()

r = requests.post("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword",
                  params={"key": a.api_key}, json={"email": a.email, "password": a.password, "returnSecureToken": True})
r.raise_for_status(); tok = r.json()["idToken"]
db = a.db_url.rstrip("/")
def put(path, obj, method="PUT"):
    requests.request(method, f"{db}{path}.json", params={"auth": tok}, data=json.dumps(obj)).raise_for_status()

def day_summary(d, rnd):
    n = lambda s: 1 + s * rnd.uniform(-1, 1)
    dec = d >= 14
    met = [6000 * (.65 if dec else 1) * n(.04), 70 * (.65 if dec else 1) * n(.05), 40 * n(.07),
           60 * n(.06), 100 * n(.02), 55 * (1.6 if dec else 1) * n(.08)]
    sym = [4 + rnd.uniform(-1, 1) for _ in range(5)]
    if d >= 20: sym[1] = sym[3] = sym[4] = 18
    return met, sym, (0.4 if d >= 17 else 0.03)

rnd = random.Random(1)
put(f"/patients/{a.pid}/demo", None, "DELETE")
base = []
for d in range(1, 27):
    met, sym, abn = day_summary(d, rnd)
    if d <= 7: base.append(met)
    bm = [round(sum(c) / len(c), 1) for c in zip(*base)]
    bs = [round(max(0.1 * m, 1e-6), 1) for m in bm]
    # simplified level progression matching the firmware demo (Watch ~day 20, Review ~day 23)
    risk = 0 if d < 20 else 1 if d < 23 else 2
    mask = 0x00 if d < 15 else 0x40 if d < 17 else 0x60 if d < 20 else 0x7A
    put(f"/patients/{a.pid}/demo/daily/{d}", {"day": d, "ts": int(time.time()), "met": [round(x, 1) for x in met],
        "sym": [round(x, 1) for x in sym], "abn": abn, "mask": mask, "risk": risk, "riskName": ["Stable", "Watch", "Review"][risk],
        "baseDone": d > 7, "demo": True, "bm": bm, "bs": bs, "cli": 2000})
    put(f"/patients/{a.pid}/live", {"ts": int(time.time()), "demo": True, "err": False, "leg": "L", "act": 1, "steps": int(met[0]),
        "cad": int(met[4]), "risk": risk, "riskName": ["Stable", "Watch", "Review"][risk], "base": min(100, d * 100 // 7),
        "baseDone": d > 7, "asym": 0, "load": d >= 17, "queued": 0})
    put(f"/index/{a.pid}", {"risk": risk, "ts": int(time.time()), "base": min(100, d * 100 // 7), "demo": True, "err": False})
    print(f"day {d}: risk={risk}"); time.sleep(a.period)
