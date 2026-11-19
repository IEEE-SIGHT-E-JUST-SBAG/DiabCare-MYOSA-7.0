#pragma once
// MYOSA MPU6050 driver (raw I2C) with validation. +-8 g, +-1000 dps, DLPF ~44 Hz.
#include <Wire.h>
#include <math.h>
#include "config.h"

struct ImuSample { float ax, ay, az, gx, gy, gz; };

class Imu {
 public:
  bool begin() {
    if (!ping()) return false;
    bool ok = wr(0x6B, 0x01); delay(10);
    ok &= wr(0x1A, 0x03);   // DLPF_CFG=3 -> ~44 Hz (below the 50 Hz Nyquist)
    ok &= wr(0x1B, 0x10);   // gyro +-1000 dps (32.8 LSB/dps)
    ok &= wr(0x1C, 0x10);   // accel +-8 g (4096 LSB/g): heel strikes exceed +-4 g
    Wire.setClock(I2C_CLOCK);
    return ok;
  }
  bool ping() { Wire.beginTransmission(MPU_ADDR); return Wire.endTransmission() == 0; }

  bool read(ImuSample& s) {
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14, (uint8_t)true) != 14) return false;
    int16_t v[7];
    for (int i = 0; i < 7; i++) { int16_t hi = Wire.read(); v[i] = (hi << 8) | Wire.read(); }
    s.ax = v[0] / 4096.0f; s.ay = v[1] / 4096.0f; s.az = v[2] / 4096.0f;
    s.gx = v[4] / 32.8f;   s.gy = v[5] / 32.8f;   s.gz = v[6] / 32.8f;
    if (fabsf(s.ax) > 8.5f || fabsf(s.ay) > 8.5f || fabsf(s.az) > 8.5f) return false;
    if (v[0] == 0 && v[1] == 0 && v[2] == 0) return false;      // dead bus
    return true;
  }
 private:
  bool wr(uint8_t r, uint8_t v) { Wire.beginTransmission(MPU_ADDR); Wire.write(r); Wire.write(v); return Wire.endTransmission() == 0; }
};
