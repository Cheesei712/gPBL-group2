#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFi.h>
#include <math.h>

#include "http_gateway.h"

#if __has_include("device_secrets.h")
#include "device_secrets.h"
#else
#define DEVICE_ID "esp32-node-01"
#define DEVICE_WIFI_SSID ""
#define DEVICE_WIFI_PASSWORD ""
#define DEVICE_SERVER_URL "http://192.168.1.100:8000/api/v1/analyze"
#define DEVICE_API_KEY ""
#endif

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
// RGB common cathode  -> R: GPIO 16, G: GPIO 17, B: GPIO 19
// Active buzzer       -> GPIO 18
// Tat ca cac module phai noi chung GND voi ESP32.

constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t HC_TRIG_PIN = 21;
constexpr uint8_t HC_ECHO_PIN = 26;
constexpr uint8_t LM35_PIN = 34;
constexpr uint8_t STEAM_PIN = 35;
constexpr uint8_t WATER_PIN = 32;
constexpr uint8_t VIBRATION_PIN = 33;
constexpr uint8_t RGB_RED_PIN = 16;
constexpr uint8_t RGB_GREEN_PIN = 17;
constexpr uint8_t RGB_BLUE_PIN = 19;
constexpr uint8_t BUZZER_PIN = 18;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
// Mot phut/request du de cap nhat output ma khong gay tai khong can thiet cho server.
constexpr uint32_t ANALYZE_INTERVAL_MS = 60000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t ULTRASONIC_TIMEOUT_US = 30000;
constexpr uint8_t ULTRASONIC_SAMPLE_COUNT = 5;
constexpr uint8_t ULTRASONIC_MIN_VALID_SAMPLES = 3;
constexpr uint32_t ULTRASONIC_GAP_MS = 40;
constexpr float ULTRASONIC_MIN_DISTANCE_CM = 2.0F;
constexpr size_t VIBRATION_SAMPLE_COUNT = 250;
constexpr uint32_t VIBRATION_SAMPLE_PERIOD_US = 1000; // Xap xi 1 kHz.
constexpr float VIBRATION_EVENT_THRESHOLD_ADC = 80.0F;
constexpr uint8_t ANALOG_SAMPLE_COUNT = 16;
constexpr int ADC_HIGH_RAIL_THRESHOLD = 4090;

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
HttpGateway gateway(DEVICE_SERVER_URL, DEVICE_API_KEY);

enum class BuzzerState { OFF, BEEP, CONTINUOUS };

class OutputController {
 public:
  void begin() {
    pinMode(RGB_RED_PIN, OUTPUT);
    pinMode(RGB_GREEN_PIN, OUTPUT);
    pinMode(RGB_BLUE_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    setRgb(false, false, true);  // Blue means waiting/unknown.
    digitalWrite(BUZZER_PIN, LOW);
  }

  void apply(const ServerAnalysis &analysis) {
    if (analysis.ledColor == "GREEN") setRgb(false, true, false);
    else if (analysis.ledColor == "YELLOW") setRgb(true, true, false);
    else if (analysis.ledColor == "RED") setRgb(true, false, false);
    else setRgb(false, false, true);

    if (analysis.buzzerMode == "CONTINUOUS") buzzerState_ = BuzzerState::CONTINUOUS;
    else if (analysis.buzzerMode == "BEEP") buzzerState_ = BuzzerState::BEEP;
    else buzzerState_ = BuzzerState::OFF;
    lastToggleMs_ = millis();
    buzzerOn_ = buzzerState_ == BuzzerState::CONTINUOUS;
    digitalWrite(BUZZER_PIN, buzzerOn_ ? HIGH : LOW);
  }

  void update() {
    if (buzzerState_ == BuzzerState::OFF) {
      digitalWrite(BUZZER_PIN, LOW);
      return;
    }
    if (buzzerState_ == BuzzerState::CONTINUOUS) {
      digitalWrite(BUZZER_PIN, HIGH);
      return;
    }
    const uint32_t now = millis();
    const uint32_t duration = buzzerOn_ ? 200 : 800;
    if (now - lastToggleMs_ >= duration) {
      buzzerOn_ = !buzzerOn_;
      lastToggleMs_ = now;
      digitalWrite(BUZZER_PIN, buzzerOn_ ? HIGH : LOW);
    }
  }

 private:
  void setRgb(bool red, bool green, bool blue) {
    digitalWrite(RGB_RED_PIN, red ? HIGH : LOW);
    digitalWrite(RGB_GREEN_PIN, green ? HIGH : LOW);
    digitalWrite(RGB_BLUE_PIN, blue ? HIGH : LOW);
  }

  BuzzerState buzzerState_ = BuzzerState::OFF;
  bool buzzerOn_ = false;
  uint32_t lastToggleMs_ = 0;
};

OutputController outputs;

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
  // KS0272 la tin hieu don cuc: raw=0 khi nghi la binh thuong, chi 4095 moi la cham tran.
  data.vibrationSaturated = maximum >= ADC_HIGH_RAIL_THRESHOLD;
  data.vibrationValid = true; // analogRead da hoan tat du VIBRATION_SAMPLE_COUNT mau.
}

int readAveragedAdc(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ANALOG_SAMPLE_COUNT; ++i) {
    sum += analogRead(pin);
    delayMicroseconds(250);
  }
  return static_cast<int>((sum + ANALOG_SAMPLE_COUNT / 2) / ANALOG_SAMPLE_COUNT);
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

uint32_t readMedianUltrasonicEchoUs() {
  uint32_t samples[ULTRASONIC_SAMPLE_COUNT];
  uint8_t validCount = 0;
  for (uint8_t i = 0; i < ULTRASONIC_SAMPLE_COUNT; ++i) {
    const uint32_t echo = readUltrasonicEchoUs();
    if (echo > 0) samples[validCount++] = echo;
    if (i + 1 < ULTRASONIC_SAMPLE_COUNT) delay(ULTRASONIC_GAP_MS);
  }
  if (validCount < ULTRASONIC_MIN_VALID_SAMPLES) return 0;

  // Sap xep mang nho de lay median, loai bo cac echo le do phan xa sai.
  for (uint8_t i = 1; i < validCount; ++i) {
    const uint32_t value = samples[i];
    int8_t j = static_cast<int8_t>(i) - 1;
    while (j >= 0 && samples[j] > value) {
      samples[j + 1] = samples[j];
      --j;
    }
    samples[j + 1] = value;
  }
  return samples[validCount / 2];
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
  data.echoTimeUs = readMedianUltrasonicEchoUs();
  if (data.echoTimeUs > 0 && !isnan(compensationTempC)) {
    const float speedOfSoundMps = 331.3F + 0.606F * compensationTempC;
    data.ultrasonicDistanceCm = data.echoTimeUs * speedOfSoundMps / 20000.0F;
    if (data.ultrasonicDistanceCm >= ULTRASONIC_MIN_DISTANCE_CM &&
        data.ultrasonicDistanceCm <= SENSOR_TO_BOTTOM_CM + 10.0F) {
      data.waterHeightCm = clampFloat(SENSOR_TO_BOTTOM_CM - data.ultrasonicDistanceCm,
                                      0.0F, SENSOR_TO_BOTTOM_CM);
      data.ultrasonicValid = true;
    }
  }

  data.steamRaw = readAveragedAdc(STEAM_PIN);
  data.steamPercent = mapPercent(data.steamRaw, STEAM_DRY_RAW, STEAM_WET_RAW);
  // 0 co the la trang thai kho; 4095 lien tuc la cham rail/nham chan DO hoac qua ap.
  data.steamValid = data.steamRaw >= 0 && data.steamRaw < ADC_HIGH_RAIL_THRESHOLD;
  data.waterRaw = readAveragedAdc(WATER_PIN);
  data.waterPercent = mapPercent(data.waterRaw, WATER_EMPTY_RAW, WATER_FULL_RAW);
  data.waterValid = data.waterRaw >= 0 && data.waterRaw < ADC_HIGH_RAIL_THRESHOLD;
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

String buildJsonPayload(const SensorData &data) {
  JsonDocument document;
  document["device_id"] = DEVICE_ID;
  document["timestamp_ms"] = millis();

  document["dht11"]["valid"] = data.dhtValid;
  document["dht11"]["temperature_c"] = data.dhtTemperatureC;
  document["dht11"]["humidity_percent"] = data.humidityPercent;
  document["lm35"]["valid"] = data.lm35Valid;
  document["lm35"]["temperature_c"] = data.lm35TemperatureC;
  document["hc_sr04"]["valid"] = data.ultrasonicValid;
  document["hc_sr04"]["echo_time_us"] = data.echoTimeUs;
  document["hc_sr04"]["distance_cm"] = data.ultrasonicDistanceCm;
  document["hc_sr04"]["water_height_cm"] = data.waterHeightCm;
  document["steam_sensor"]["valid"] = data.steamValid;
  document["steam_sensor"]["adc_raw"] = data.steamRaw;
  document["steam_sensor"]["wet_percent"] = data.steamPercent;
  document["water_sensor"]["valid"] = data.waterValid;
  document["water_sensor"]["adc_raw"] = data.waterRaw;
  document["water_sensor"]["level_percent"] = data.waterPercent;
  document["ks0272_vibration"]["valid"] = data.vibrationValid;
  document["ks0272_vibration"]["current_raw"] = data.vibrationCurrentRaw;
  document["ks0272_vibration"]["min_raw"] = data.vibrationMinRaw;
  document["ks0272_vibration"]["max_raw"] = data.vibrationMaxRaw;
  document["ks0272_vibration"]["peak_to_peak_raw"] = data.vibrationPeakToPeak;
  document["ks0272_vibration"]["mean_raw"] = data.vibrationMeanRaw;
  document["ks0272_vibration"]["rms_raw"] = data.vibrationRmsRaw;
  document["ks0272_vibration"]["event_count"] = data.vibrationEventCount;
  document["ks0272_vibration"]["saturated"] = data.vibrationSaturated;
  document["all_sensors_valid"] = allSensorsValid(data);

  String payload;
  serializeJson(document, payload);
  return payload;
}

void printReport(const SensorData &data, const String &payload) {
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

  Serial.printf("STEAM/RAIN | ADC raw: %d/4095 | Muc uot: %.1f %% | %s\n",
                data.steamRaw, data.steamPercent, data.steamValid ? "OK" : "KET RAIL");
  Serial.printf("WATER      | ADC raw: %d/4095 | Muc nuoc: %.1f %% | %s\n",
                data.waterRaw, data.waterPercent, data.waterValid ? "OK" : "KET RAIL");

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
  if (!data.steamValid) Serial.println("KHUYEN CAO  | STEAM cham rail cao; kiem tra AO/DO va dien ap tin hieu.");
  if (!data.waterValid) Serial.println("KHUYEN CAO  | WATER cham rail cao; kiem tra AO/DO va dien ap tin hieu.");
  if (!data.vibrationValid) Serial.println("KHUYEN CAO  | Kiem tra chan S/nguon cua KS0272.");
  if (data.vibrationSaturated) Serial.println("KHUYEN CAO  | KS0272 cham tran ADC 4095; tin hieu rung dang bi cat dinh.");
  Serial.print("JSON_DATA: ");
  Serial.println(payload);
  Serial.println("============================================================");
}

bool deviceConfigReady() {
  return strlen(DEVICE_ID) > 0 && strlen(DEVICE_WIFI_SSID) > 0 &&
         strlen(DEVICE_SERVER_URL) > 0 && strlen(DEVICE_API_KEY) > 0 &&
         strcmp(DEVICE_WIFI_SSID, "your-wifi-name") != 0 &&
         strcmp(DEVICE_API_KEY, "replace-with-the-same-key-as-backend") != 0;
}

void connectWifi() {
  if (!deviceConfigReady()) {
    Serial.println("HTTP disabled: device_secrets.h is missing or still has placeholder values.");
    Serial.println("Copy include/device_secrets.example.h to include/device_secrets.h and edit it.");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(DEVICE_WIFI_SSID, DEVICE_WIFI_PASSWORD);
  Serial.printf("Connecting WiFi to %s", DEVICE_WIFI_SSID);
  for (uint8_t i = 0; i < 20 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println(" not connected; firmware will retry.");
  }
}

void requestAnalysis(const String &payload) {
  ServerAnalysis analysis;
  String error;
  Serial.printf("POST %s\n", DEVICE_SERVER_URL);
  if (!gateway.analyze(payload, analysis, error)) {
    Serial.printf("SERVER ERROR | %s\n", error.c_str());
    return;  // Keep the last known safe output state.
  }

  Serial.printf("SERVER RESULT | risk=%s | hazard=%s | confidence=%d%%\n",
                analysis.riskLevel.c_str(), analysis.hazard.c_str(),
                analysis.confidencePercent);
  Serial.printf("ADVICE        | %s\n", analysis.advice.c_str());
  Serial.printf("REASON        | %s\n", analysis.reason.c_str());
  Serial.printf("OUTPUTS       | LED=%s | buzzer=%s\n",
                analysis.ledColor.c_str(), analysis.buzzerMode.c_str());
  outputs.apply(analysis);
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
  outputs.begin();
  connectWifi();

  Serial.println("\nESP32 MULTI-HAZARD SENSOR NODE");
  Serial.printf("Device ID: %s\n", DEVICE_ID);
  Serial.printf("Serial: %lu baud | Chu ky: %lu ms\n", SERIAL_BAUD, SAMPLE_INTERVAL_MS);
  Serial.printf("KS0272: GPIO %u | %u mau/cua so | threshold: %.0f ADC\n",
                VIBRATION_PIN, VIBRATION_SAMPLE_COUNT, VIBRATION_EVENT_THRESHOLD_ADC);
  Serial.println("Luu y: hay hieu chuan cac hang *_RAW va SENSOR_TO_BOTTOM_CM.");
}

void loop() {
  static uint32_t lastSampleMs = 0;
  static uint32_t lastAnalyzeMs = 0;
  static uint32_t lastWifiRetryMs = 0;
  outputs.update();

  const uint32_t now = millis();
  if (deviceConfigReady() && WiFi.status() != WL_CONNECTED &&
      now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
    lastWifiRetryMs = now;
    WiFi.reconnect();
  }
  if (lastSampleMs == 0 || now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    const SensorData data = readAllSensors();
    const String payload = buildJsonPayload(data);
    printReport(data, payload);
    if (WiFi.status() == WL_CONNECTED &&
        (lastAnalyzeMs == 0 || now - lastAnalyzeMs >= ANALYZE_INTERVAL_MS)) {
      lastAnalyzeMs = now;
      requestAnalysis(payload);
    }
  }
}
