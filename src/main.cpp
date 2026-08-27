#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <math.h>

// ================================================================
// SO DO CHAN - ESP32 DEV MODULE
// ================================================================
// DHT11 DATA       -> GPIO 4  (them dien tro keo len 10 kOhm neu cam bien roi)
// HC-SR04 TRIG     -> GPIO 25
// HC-SR04 ECHO     -> GPIO 26 (BAT BUOC ha 5 V xuong 3.3 V bang cau chia ap)
// LM35 OUT         -> GPIO 34 (ADC1, input only)
// Steam/Rain AO    -> GPIO 35 (ADC1, input only, cap module bang 3.3 V)
// Water level AO   -> GPIO 32 (ADC1, cap module bang 3.3 V)
// MPU6050 SDA      -> GPIO 21
// MPU6050 SCL      -> GPIO 22
// Tat ca cac module phai noi chung GND voi ESP32.

constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t HC_TRIG_PIN = 25;
constexpr uint8_t HC_ECHO_PIN = 26;
constexpr uint8_t LM35_PIN = 34;
constexpr uint8_t STEAM_PIN = 35;
constexpr uint8_t WATER_PIN = 32;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;

constexpr uint8_t MPU6050_ADDRESS = 0x68;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr uint32_t ULTRASONIC_TIMEOUT_US = 30000;

// Khoang cach tu HC-SR04 den moc day khi khong co nuoc.
// Can do thuc te va thay gia tri nay sau khi lap cam bien.
constexpr float SENSOR_TO_BOTTOM_CM = 100.0F;

// Hieu chuan ADC: ghi lai gia tri raw khi kho va khi uot/ngap toi da.
// Tuy module, gia tri co the tang hoac giam khi uot. Ham mapPercent tu xu ly ca hai.
constexpr int STEAM_DRY_RAW = 0;
constexpr int STEAM_WET_RAW = 3000;
constexpr int WATER_EMPTY_RAW = 0;
constexpr int WATER_FULL_RAW = 3000;

DHT dht(DHT_PIN, DHT11);

struct SensorData {
  float dhtTemperatureC = NAN;
  float humidityPercent = NAN;
  float lm35TemperatureC = NAN;
  uint32_t echoTimeUs = 0;
  float ultrasonicDistanceCm = NAN;
  float waterHeightCm = NAN;
  int steamRaw = 0;
  float steamPercent = 0.0F;
  int waterRaw = 0;
  float waterPercent = 0.0F;
  float accelXG = NAN;
  float accelYG = NAN;
  float accelZG = NAN;
  float accelMagnitudeG = NAN;
  float dynamicAccelerationG = NAN;
  float tiltXDeg = NAN;
  float tiltYDeg = NAN;
  bool dhtValid = false;
  bool lm35Valid = false;
  bool ultrasonicValid = false;
  bool steamValid = false;
  bool waterValid = false;
  bool mpuValid = false;
};

float clampFloat(float value, float minimum, float maximum) {
  return fminf(maximum, fmaxf(minimum, value));
}

float mapPercent(int raw, int rawAtZero, int rawAtFull) {
  if (rawAtZero == rawAtFull) return 0.0F;
  const float percent = 100.0F * (raw - rawAtZero) / (rawAtFull - rawAtZero);
  return clampFloat(percent, 0.0F, 100.0F);
}

bool writeMpuRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool initializeMpu6050() {
  // PWR_MGMT_1 = 0: danh thuc MPU6050; ACCEL_CONFIG = 0: thang do +/-2 g.
  return writeMpuRegister(0x6B, 0x00) && writeMpuRegister(0x1C, 0x00);
}

bool readMpu6050(SensorData &data) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(0x3B); // Thanh ghi ACCEL_XOUT_H.
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU6050_ADDRESS, static_cast<size_t>(6), true) != 6) return false;

  const int16_t rawX = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
  const int16_t rawY = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
  const int16_t rawZ = static_cast<int16_t>((Wire.read() << 8) | Wire.read());

  constexpr float ACCEL_SCALE = 16384.0F; // LSB/g o thang +/-2 g.
  data.accelXG = rawX / ACCEL_SCALE;
  data.accelYG = rawY / ACCEL_SCALE;
  data.accelZG = rawZ / ACCEL_SCALE;
  data.accelMagnitudeG = sqrtf(data.accelXG * data.accelXG +
                               data.accelYG * data.accelYG +
                               data.accelZG * data.accelZG);
  data.dynamicAccelerationG = fabsf(data.accelMagnitudeG - 1.0F);
  data.tiltXDeg = atan2f(data.accelYG,
                         sqrtf(data.accelXG * data.accelXG + data.accelZG * data.accelZG)) *
                  180.0F / PI;
  data.tiltYDeg = atan2f(-data.accelXG,
                         sqrtf(data.accelYG * data.accelYG + data.accelZG * data.accelZG)) *
                  180.0F / PI;
  return true;
}

float readLm35Celsius() {
  // Lay trung binh 32 mau de giam nhieu ADC.
  uint32_t totalMillivolts = 0;
  constexpr uint8_t SAMPLE_COUNT = 32;
  for (uint8_t i = 0; i < SAMPLE_COUNT; ++i) {
    totalMillivolts += analogReadMilliVolts(LM35_PIN);
    delay(2);
  }
  const float averageMillivolts = totalMillivolts / static_cast<float>(SAMPLE_COUNT);
  return averageMillivolts / 10.0F; // LM35: 10 mV / do C.
}

uint32_t readUltrasonicEchoUs() {
  digitalWrite(HC_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(HC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(HC_TRIG_PIN, LOW);
  return pulseIn(HC_ECHO_PIN, HIGH, ULTRASONIC_TIMEOUT_US);
}

SensorData readAllSensors() {
  SensorData data;

  data.dhtTemperatureC = dht.readTemperature();
  data.humidityPercent = dht.readHumidity();
  data.dhtValid = !isnan(data.dhtTemperatureC) && !isnan(data.humidityPercent);

  data.lm35TemperatureC = readLm35Celsius();
  // LM35 noi truc tiep chi do duoc tu 0 C; gioi han tren de bat loi day/ADC ro rang.
  data.lm35Valid = !isnan(data.lm35TemperatureC) &&
                   data.lm35TemperatureC >= 0.0F && data.lm35TemperatureC <= 150.0F;

  // Uu tien LM35 cho bu toc van toc am; neu LM35 loi thi dung DHT11.
  const float compensationTempC = data.lm35Valid
                                      ? data.lm35TemperatureC
                                      : data.dhtTemperatureC;
  data.echoTimeUs = readUltrasonicEchoUs();
  if (data.echoTimeUs > 0 && !isnan(compensationTempC)) {
    const float speedOfSoundMps = 331.3F + 0.606F * compensationTempC;
    data.ultrasonicDistanceCm = data.echoTimeUs * speedOfSoundMps / 20000.0F;
    data.waterHeightCm = clampFloat(SENSOR_TO_BOTTOM_CM - data.ultrasonicDistanceCm,
                                    0.0F, SENSOR_TO_BOTTOM_CM);
    data.ultrasonicValid = true;
  }

  data.steamRaw = analogRead(STEAM_PIN);
  data.steamPercent = mapPercent(data.steamRaw, STEAM_DRY_RAW, STEAM_WET_RAW);
  data.steamValid = data.steamRaw >= 0 && data.steamRaw <= 4095;
  data.waterRaw = analogRead(WATER_PIN);
  data.waterPercent = mapPercent(data.waterRaw, WATER_EMPTY_RAW, WATER_FULL_RAW);
  data.waterValid = data.waterRaw >= 0 && data.waterRaw <= 4095;
  data.mpuValid = readMpu6050(data);
  return data;
}

bool allSensorsValid(const SensorData &data) {
  return data.dhtValid && data.lm35Valid && data.ultrasonicValid &&
         data.steamValid && data.waterValid && data.mpuValid;
}

void printFloatOrError(float value, uint8_t decimals = 2) {
  if (isnan(value)) Serial.print("N/A");
  else Serial.print(value, decimals);
}

void printJsonNumber(float value, uint8_t decimals = 3) {
  if (isnan(value) || isinf(value)) Serial.print("null");
  else Serial.print(value, decimals);
}

void printJsonPayload(const SensorData &data) {
  // Mot object JSON tren mot dong, san sang lam payload dau vao cho LLM/API.
  Serial.print("JSON_DATA: {");
  Serial.printf("\"timestamp_ms\":%lu,", millis());

  Serial.print("\"dht11\":{\"valid\":");
  Serial.print(data.dhtValid ? "true" : "false");
  Serial.print(",\"temperature_c\":"); printJsonNumber(data.dhtTemperatureC);
  Serial.print(",\"humidity_percent\":"); printJsonNumber(data.humidityPercent);
  Serial.print("},");

  Serial.print("\"lm35\":{\"valid\":");
  Serial.print(data.lm35Valid ? "true" : "false");
  Serial.print(",\"temperature_c\":"); printJsonNumber(data.lm35TemperatureC);
  Serial.print("},");

  Serial.print("\"hc_sr04\":{\"valid\":");
  Serial.print(data.ultrasonicValid ? "true" : "false");
  Serial.printf(",\"echo_time_us\":%lu,\"distance_cm\":", data.echoTimeUs);
  printJsonNumber(data.ultrasonicDistanceCm);
  Serial.print(",\"water_height_cm\":"); printJsonNumber(data.waterHeightCm);
  Serial.print("},");

  Serial.print("\"steam_sensor\":{\"valid\":");
  Serial.print(data.steamValid ? "true" : "false");
  Serial.printf(",\"adc_raw\":%d,\"wet_percent\":%.2f},",
                data.steamRaw, data.steamPercent);

  Serial.print("\"water_sensor\":{\"valid\":");
  Serial.print(data.waterValid ? "true" : "false");
  Serial.printf(",\"adc_raw\":%d,\"level_percent\":%.2f},",
                data.waterRaw, data.waterPercent);

  Serial.print("\"mpu6050\":{\"valid\":");
  Serial.print(data.mpuValid ? "true" : "false");
  Serial.print(",\"accel_x_g\":"); printJsonNumber(data.accelXG);
  Serial.print(",\"accel_y_g\":"); printJsonNumber(data.accelYG);
  Serial.print(",\"accel_z_g\":"); printJsonNumber(data.accelZG);
  Serial.print(",\"magnitude_g\":"); printJsonNumber(data.accelMagnitudeG);
  Serial.print(",\"dynamic_g\":"); printJsonNumber(data.dynamicAccelerationG);
  Serial.print(",\"tilt_x_deg\":"); printJsonNumber(data.tiltXDeg);
  Serial.print(",\"tilt_y_deg\":"); printJsonNumber(data.tiltYDeg);
  Serial.print("},");

  Serial.print("\"all_sensors_valid\":");
  Serial.print(allSensorsValid(data) ? "true" : "false");
  Serial.println("}");
}

void printReport(const SensorData &data) {
  Serial.println();
  Serial.println("============================================================");
  Serial.printf("BAO CAO CAM BIEN | uptime: %lu ms\n", millis());
  Serial.println("------------------------------------------------------------");

  Serial.print("DHT11      | Nhiet do: ");
  printFloatOrError(data.dhtTemperatureC);
  Serial.print(" C | Do am: ");
  printFloatOrError(data.humidityPercent);
  Serial.printf(" %% | %s\n", data.dhtValid ? "OK" : "LOI DOC");

  Serial.print("LM35       | Nhiet do: ");
  printFloatOrError(data.lm35TemperatureC);
  Serial.printf(" C | trung binh 32 mau ADC | %s\n", data.lm35Valid ? "OK" : "LOI DOC");

  Serial.printf("HC-SR04    | Echo: %lu us | Khoang cach: ", data.echoTimeUs);
  printFloatOrError(data.ultrasonicDistanceCm);
  Serial.print(" cm | Chieu cao nuoc: ");
  printFloatOrError(data.waterHeightCm);
  Serial.printf(" cm | %s\n", data.ultrasonicValid ? "OK" : "TIMEOUT/LOI");

  Serial.printf("STEAM/RAIN | ADC raw: %d/4095 | Muc uot: %.1f %%\n",
                data.steamRaw, data.steamPercent);
  Serial.printf("WATER      | ADC raw: %d/4095 | Muc nuoc: %.1f %%\n",
                data.waterRaw, data.waterPercent);

  Serial.print("MPU6050    | ax/ay/az: ");
  printFloatOrError(data.accelXG, 3); Serial.print(" / ");
  printFloatOrError(data.accelYG, 3); Serial.print(" / ");
  printFloatOrError(data.accelZG, 3); Serial.print(" g | magnitude: ");
  printFloatOrError(data.accelMagnitudeG, 3); Serial.print(" g | dynamic: ");
  printFloatOrError(data.dynamicAccelerationG, 3); Serial.println(" g");
  Serial.print("            | Nghieng X/Y: ");
  printFloatOrError(data.tiltXDeg, 1); Serial.print(" / ");
  printFloatOrError(data.tiltYDeg, 1);
  Serial.printf(" deg | %s\n", data.mpuValid ? "OK" : "KHONG TIM THAY");

  Serial.println("------------------------------------------------------------");
  Serial.printf("TRANG THAI  | Tat ca cam bien: %s\n",
                allSensorsValid(data) ? "DOC THANH CONG" : "CO CAM BIEN LOI");
  if (!data.dhtValid) Serial.println("KHUYEN CAO  | Kiem tra day DATA/nguon cua DHT11.");
  if (!data.lm35Valid) Serial.println("KHUYEN CAO  | Kiem tra OUT/nguon va hieu chuan LM35.");
  if (!data.ultrasonicValid) Serial.println("KHUYEN CAO  | Kiem tra HC-SR04 va cau chia ap chan ECHO.");
  if (!data.mpuValid) Serial.println("KHUYEN CAO  | Kiem tra SDA/SCL va dia chi MPU6050 0x68.");
  printJsonPayload(data);
  Serial.println("============================================================");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  pinMode(HC_TRIG_PIN, OUTPUT);
  pinMode(HC_ECHO_PIN, INPUT);
  digitalWrite(HC_TRIG_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(LM35_PIN, ADC_11db);
  analogSetPinAttenuation(STEAM_PIN, ADC_11db);
  analogSetPinAttenuation(WATER_PIN, ADC_11db);

  dht.begin();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
  const bool mpuReady = initializeMpu6050();

  Serial.println("\nESP32 MULTI-HAZARD SENSOR NODE");
  Serial.printf("Serial: %lu baud | Chu ky: %lu ms\n", SERIAL_BAUD, SAMPLE_INTERVAL_MS);
  Serial.printf("MPU6050 khoi tao: %s\n", mpuReady ? "THANH CONG" : "THAT BAI");
  Serial.println("Luu y: hay hieu chuan cac hang *_RAW va SENSOR_TO_BOTTOM_CM.");
}

void loop() {
  static uint32_t lastSampleMs = 0;
  const uint32_t now = millis();
  if (lastSampleMs == 0 || now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    const SensorData data = readAllSensors();
    printReport(data);
  }
}
