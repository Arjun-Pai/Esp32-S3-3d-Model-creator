// MPU-6050 test for ESP32-S3 (no library needed)
// Wiring: VCC->3V3, GND->GND, SDA->GPIO1, SCL->GPIO2, AD0->GND
// Serial Monitor: 115200 baud

#include <Wire.h>

#define SDA_PIN 1
#define SCL_PIN 2
#define MPU_ADDR 0x68

#define REG_WHO_AM_I    0x75
#define REG_PWR_MGMT_1  0x6B
#define REG_GYRO_CONFIG 0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT_H 0x3B

float gyroOffX = 0, gyroOffY = 0, gyroOffZ = 0;
int errorCount = 0;
int zeroCount = 0;

void printI2CError(uint8_t err) {
  Serial.print("ERROR: I2C failed - ");
  switch (err) {
    case 1: Serial.println("data too long for buffer"); break;
    case 2: Serial.println("no response at address (NACK). Check VCC, GND, SDA/SCL wiring and AD0"); break;
    case 3: Serial.println("NACK on data. Sensor stopped responding mid-transfer"); break;
    case 4: Serial.println("unknown bus error. Check for loose wires or shorts"); break;
    case 5: Serial.println("timeout. Bus is stuck, check SDA/SCL wires and pull-ups"); break;
    default: Serial.printf("code %u\n", err); break;
  }
}

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  uint8_t err = Wire.endTransmission();
  if (err) {
    printI2CError(err);
    return false;
  }
  return true;
}

bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  uint8_t err = Wire.endTransmission(false);
  if (err) {
    printI2CError(err);
    return false;
  }
  uint8_t got = Wire.requestFrom((uint8_t)MPU_ADDR, len);
  if (got != len) {
    Serial.printf("ERROR: expected %u bytes but got %u\n", len, got);
    return false;
  }
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void scanBus() {
  Serial.println("Scanning I2C bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("ERROR: no I2C devices found. Check 3V3, GND, SDA (GPIO1), SCL (GPIO2).");
  } else {
    Serial.printf("Scan done: %d device(s)\n", found);
  }
}

bool initMPU() {
  uint8_t id = 0;
  if (!readRegs(REG_WHO_AM_I, &id, 1)) {
    Serial.println("ERROR: could not read WHO_AM_I. MPU-6050 not responding.");
    return false;
  }
  Serial.printf("WHO_AM_I = 0x%02X\n", id);
  if (id != 0x68) {
    Serial.println("WARNING: expected 0x68. May be a clone or a different chip, readings may still work.");
  }
  if (!writeReg(REG_PWR_MGMT_1, 0x00)) return false;  // wake from sleep
  delay(100);
  if (!writeReg(REG_GYRO_CONFIG, 0x00)) return false;   // +-250 dps
  if (!writeReg(REG_ACCEL_CONFIG, 0x00)) return false;  // +-2 g
  Serial.println("MPU-6050 initialized OK");
  return true;
}

bool readRaw(int16_t *ax, int16_t *ay, int16_t *az, int16_t *t, int16_t *gx, int16_t *gy, int16_t *gz) {
  uint8_t b[14];
  if (!readRegs(REG_ACCEL_XOUT_H, b, 14)) return false;
  *ax = (b[0] << 8) | b[1];
  *ay = (b[2] << 8) | b[3];
  *az = (b[4] << 8) | b[5];
  *t  = (b[6] << 8) | b[7];
  *gx = (b[8] << 8) | b[9];
  *gy = (b[10] << 8) | b[11];
  *gz = (b[12] << 8) | b[13];
  return true;
}

void calibrateGyro() {
  Serial.println("Calibrating gyro in 2 seconds. Keep the sensor still...");
  delay(2000);
  long sx = 0, sy = 0, sz = 0;
  int good = 0;
  for (int i = 0; i < 200; i++) {
    int16_t ax, ay, az, t, gx, gy, gz;
    if (readRaw(&ax, &ay, &az, &t, &gx, &gy, &gz)) {
      sx += gx; sy += gy; sz += gz;
      good++;
    }
    delay(5);
  }
  if (good < 100) {
    Serial.println("ERROR: calibration failed, too many bad reads. Offsets set to 0.");
    return;
  }
  gyroOffX = (sx / (float)good) / 131.0;
  gyroOffY = (sy / (float)good) / 131.0;
  gyroOffZ = (sz / (float)good) / 131.0;
  Serial.printf("Gyro offsets (dps): X=%.2f Y=%.2f Z=%.2f\n", gyroOffX, gyroOffY, gyroOffZ);
}

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(500);

  Serial.println();
  Serial.println("=== MPU-6050 test (ESP32-S3) ===");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(100);

  scanBus();
  if (initMPU()) {
    calibrateGyro();
  } else {
    Serial.println("Setup failed. Will keep retrying in loop.");
  }
  Serial.println("Starting readings...");
}

void loop() {
  int16_t ax, ay, az, t, gx, gy, gz;

  if (!readRaw(&ax, &ay, &az, &t, &gx, &gy, &gz)) {
    errorCount++;
    Serial.printf("Read failed (%d in a row)\n", errorCount);
    if (errorCount >= 5) {
      Serial.println("Too many errors. Re-initializing sensor...");
      if (initMPU()) errorCount = 0;
    }
    delay(1000);
    return;
  }
  errorCount = 0;

  if (ax == 0 && ay == 0 && az == 0 && gx == 0 && gy == 0 && gz == 0) {
    zeroCount++;
    if (zeroCount >= 5) {
      Serial.println("WARNING: all readings are zero. Sensor may be asleep or wiring is bad.");
      zeroCount = 0;
    }
  } else {
    zeroCount = 0;
  }

  float accX = ax / 16384.0;
  float accY = ay / 16384.0;
  float accZ = az / 16384.0;
  float gyrX = gx / 131.0 - gyroOffX;
  float gyrY = gy / 131.0 - gyroOffY;
  float gyrZ = gz / 131.0 - gyroOffZ;
  float tempC = t / 340.0 + 36.53;

  Serial.printf("Accel(g) X=%6.2f Y=%6.2f Z=%6.2f | Gyro(dps) X=%7.2f Y=%7.2f Z=%7.2f | Temp=%.1f C\n",
                accX, accY, accZ, gyrX, gyrY, gyrZ, tempC);

  delay(200);
}
