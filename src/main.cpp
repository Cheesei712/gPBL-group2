#include <Arduino.h>
#include <DHT.h>
#include <math.h>

// ================================================================
// SO DO CHAN - ESP32 DEV MODULE
// ================================================================
// DHT11 DATA       -> GPIO 4  (them dien tro keo len 10 kOhm neu cam bien roi)
// HC-SR04 TRIG     -> GPIO 21
// HC-SR04 ECHO     -> GPIO 26 (BAT BUOC ha 5 V xuong 3.3 V bang cau chia ap)
// LM35 OUT         -> GPIO 34 (ADC1, input only)
// Steam/Rain AO    -> GPIO 35 (ADC1, input only, cap module bang 3.3 V)
// Water level AO   -> GPIO 32 (ADC1, cap module bang 3.3 V)
// KS0272 Vibration S -> GPIO 33 (ADC1, cap module bang 3.3 V)
// Tat ca cac module phai noi chung GND voi ESP32.

constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t HC_TRIG_PIN = 21;
constexpr uint8_t HC_ECHO_PIN = 26;
constexpr uint8_t LM35_PIN = 34;
constexpr uint8_t STEAM_PIN = 35;
constexpr uint8_t WATER_PIN = 32;
constexpr uint8_t VIBRATION_PIN = 33;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr uint32_t ULTRASONIC_TIMEOUT_US = 30000;
constexpr size_t VIBRATION_SAMPLE_COUNT = 250;
constexpr uint32_t VIBRATION_SAMPLE_PERIOD_US = 1000; // Xap xi 1 kHz.
constexpr float VIBRATION_EVENT_THRESHOLD_ADC = 80.0F;

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
  int vibrationCurrentRaw = 0;
  int vibrationMinRaw = 0;
  int vibrationMaxRaw = 0;
  int vibrationPeakToPeak = 0;
  float vibrationMeanRaw = 0.0F;
  float vibrationRmsRaw = 0.0F;
  uint16_t vibrationEventCount = 0;
  bool vibrationSaturated = false;
  bool dhtValid = false;
  bool lm35Valid = false;
  bool ultrasonicValid = false;
  bool steamValid = false;
  bool waterValid = false;
  bool vibrationValid = false;
};

float clampFloat(float value, float minimum, float maximum) {
  return fminf(maximum, fmaxf(minimum, value));
}

float mapPercent(int raw, int rawAtZero, int rawAtFull) {
  if (rawAtZero == rawAtFull) return 0.0F;
  const float percent = 100.0F * (raw - rawAtZero) / (rawAtFull - rawAtZero);
  return clampFloat(percent, 0.0F, 100.0F);
}

void readCeramicVibration(SensorData &data) {
  // Luu mot cua so mau de tinh bien do va RMS quanh gia tri nen cua cam bien.
  uint16_t samples[VIBRATION_SAMPLE_COUNT];
  uint32_t sum = 0;
  int minimum = 4095;
  int maximum = 0;

  for (size_t i = 0; i < VIBRATION_SAMPLE_COUNT; ++i) {
    const int raw = analogRead(VIBRATION_PIN);
    samples[i] = static_cast<uint16_t>(raw);
    sum += raw;
    minimum = min(minimum, raw);
    maximum = max(maximum, raw);
    delayMicroseconds(VIBRATION_SAMPLE_PERIOD_US);
  }

  const float mean = sum / static_cast<float>(VIBRATION_SAMPLE_COUNT);
  double squaredDeviationSum = 0.0;
  bool previousAboveThreshold = false;
  uint16_t eventCount = 0;

  for (size_t i = 0; i < VIBRATION_SAMPLE_COUNT; ++i) {
    const float deviation = samples[i] - mean;
    squaredDeviationSum += static_cast<double>(deviation) * deviation;
    const bool aboveThreshold = fabsf(deviation) >= VIBRATION_EVENT_THRESHOLD_ADC;
    if (aboveThreshold && !previousAboveThreshold) ++eventCount;
    previousAboveThreshold = aboveThreshold;
  }

  data.vibrationCurrentRaw = samples[VIBRATION_SAMPLE_COUNT - 1];
  data.vibrationMinRaw = minimum;
  data.vibrationMaxRaw = maximum;
  data.vibrationPeakToPeak = maximum - minimum;
  data.vibrationMeanRaw = mean;
  data.vibrationRmsRaw = sqrtf(squaredDeviationSum / VIBRATION_SAMPLE_COUNT);
  data.vibrationEventCount = eventCount;
  data.vibrationSaturated = minimum == 0 || maximum == 4095;
  data.vibrationValid = true; // analogRead da hoan tat du VIBRATION_SAMPLE_COUNT mau.
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
  readCeramicVibration(data);
  return data;
}

bool allSensorsValid(const SensorData &data) {
  return data.dhtValid && data.lm35Valid && data.ultrasonicValid &&
         data.steamValid && data.waterValid && data.vibrationValid;
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

  Serial.print("\"ks0272_vibration\":{\"valid\":");
  Serial.print(data.vibrationValid ? "true" : "false");
  Serial.printf(",\"current_raw\":%d,\"min_raw\":%d,\"max_raw\":%d,",
                data.vibrationCurrentRaw, data.vibrationMinRaw, data.vibrationMaxRaw);
  Serial.printf("\"peak_to_peak_raw\":%d,\"mean_raw\":%.2f,\"rms_raw\":%.2f,",
                data.vibrationPeakToPeak, data.vibrationMeanRaw, data.vibrationRmsRaw);
  Serial.printf("\"event_count\":%u,\"saturated\":%s},",
                data.vibrationEventCount, data.vibrationSaturated ? "true" : "false");

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

  Serial.printf("KS0272     | Raw: %d | Min/Max: %d/%d | Peak-to-peak: %d\n",
                data.vibrationCurrentRaw, data.vibrationMinRaw,
                data.vibrationMaxRaw, data.vibrationPeakToPeak);
  Serial.printf("            | Mean: %.2f | RMS: %.2f | Events: %u | Saturated: %s | %s\n",
                data.vibrationMeanRaw, data.vibrationRmsRaw,
                data.vibrationEventCount, data.vibrationSaturated ? "YES" : "NO",
                data.vibrationValid ? "OK" : "LOI DOC");

  Serial.println("------------------------------------------------------------");
  Serial.printf("TRANG THAI  | Tat ca cam bien: %s\n",
                allSensorsValid(data) ? "DOC THANH CONG" : "CO CAM BIEN LOI");
  if (!data.dhtValid) Serial.println("KHUYEN CAO  | Kiem tra day DATA/nguon cua DHT11.");
  if (!data.lm35Valid) Serial.println("KHUYEN CAO  | Kiem tra OUT/nguon va hieu chuan LM35.");
  if (!data.ultrasonicValid) Serial.println("KHUYEN CAO  | Kiem tra HC-SR04 va cau chia ap chan ECHO.");
  if (!data.vibrationValid) Serial.println("KHUYEN CAO  | Kiem tra chan S/nguon cua KS0272.");
  if (data.vibrationSaturated) Serial.println("KHUYEN CAO  | Tin hieu KS0272 cham bien ADC 0/4095; kiem tra nguon va day S.");
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
  analogSetPinAttenuation(VIBRATION_PIN, ADC_11db);

  dht.begin();

  Serial.println("\nESP32 MULTI-HAZARD SENSOR NODE");
  Serial.printf("Serial: %lu baud | Chu ky: %lu ms\n", SERIAL_BAUD, SAMPLE_INTERVAL_MS);
  Serial.printf("KS0272: GPIO %u | %u mau/cua so | threshold: %.0f ADC\n",
                VIBRATION_PIN, VIBRATION_SAMPLE_COUNT, VIBRATION_EVENT_THRESHOLD_ADC);
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
