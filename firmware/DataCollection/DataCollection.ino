// DiabCare data collection for Edge Impulse: streams "ax,ay,az" (g) at 100 Hz.
//   edge-impulse-data-forwarder --frequency 100      (axes: ax,ay,az)
// Labels: static, walking, other. Collect in g with the SAME range as the firmware (+-8 g, DLPF 44 Hz).
#include <Wire.h>
#define I2C_SDA 21
#define I2C_SCL 22
#define MPU_ADDR 0x68
uint32_t last = 0;
static void wr(uint8_t r, uint8_t v) { Wire.beginTransmission(MPU_ADDR); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
void setup() {
  Serial.begin(115200); Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(400000);
  wr(0x6B, 0x01); delay(10); wr(0x1A, 0x03); wr(0x1C, 0x10); last = micros();
}
void loop() {
  if ((uint32_t)(micros() - last) < 10000UL) return;
  last += 10000UL;
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B); Wire.endTransmission(false);
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6, (uint8_t)true) != 6) return;
  int16_t v[3]; for (int i = 0; i < 3; i++) { int16_t hi = Wire.read(); v[i] = (hi << 8) | Wire.read(); }
  Serial.print(v[0] / 4096.0f, 4); Serial.print(','); Serial.print(v[1] / 4096.0f, 4); Serial.print(','); Serial.println(v[2] / 4096.0f, 4);
}
