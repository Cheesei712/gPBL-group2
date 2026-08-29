#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>

#include "firebase_gateway.h"
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

#ifndef FIREBASE_URL
#define FIREBASE_URL "https://gpbl-group2-default-rtdb.asia-southeast1.firebasedatabase.app"
#endif

#ifndef FIREBASE_AUTH
#define FIREBASE_AUTH ""
#endif


// ================================================================
// SO DO CHAN - ESP32 DEV MODULE
// ================================================================
// DHT11 DATA       -> GPIO 4  (them dien tro keo len 10 kOhm neu cam bien roi)
// LCD I2C SDA/SCL  -> GPIO 21 / GPIO 22
// HC-SR04 TRIG     -> GPIO 23 (GPIO 21 da danh cho I2C SDA)
// HC-SR04 ECHO     -> GPIO 26 (BAT BUOC ha 5 V xuong 3.3 V bang cau chia ap)
// LM35 OUT         -> GPIO 34 (ADC1, input only)
// Steam/Rain AO    -> GPIO 35 (ADC1, input only, cap module bang 3.3 V)
// Water level AO   -> GPIO 32 (ADC1, cap module bang 3.3 V)
// KS0272 Vibration S -> GPIO 36 (ADC1, input only, GPIO 33 da danh cho RGB Green)
// RGB common VCC/anode -> R: GPIO 25, G: GPIO 33, B: GPIO 27 (active LOW)
// Active buzzer       -> GPIO 18
// Tat ca cac module phai noi chung GND voi ESP32.

constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t HC_TRIG_PIN = 23;
constexpr uint8_t HC_ECHO_PIN = 26;
constexpr uint8_t LM35_PIN = 34;
constexpr uint8_t STEAM_PIN = 35;
constexpr uint8_t WATER_PIN = 32;
constexpr uint8_t VIBRATION_PIN = 36;
constexpr uint8_t RGB_RED_PIN = 25;
constexpr uint8_t RGB_GREEN_PIN = 33;
constexpr uint8_t RGB_BLUE_PIN = 27;
constexpr uint8_t BUZZER_PIN = 18;
constexpr uint8_t RGB_RED_PWM_CHANNEL = 0;
constexpr uint8_t RGB_GREEN_PWM_CHANNEL = 1;
constexpr uint8_t RGB_BLUE_PWM_CHANNEL = 2;
constexpr uint16_t RGB_PWM_FREQUENCY_HZ = 5000;
constexpr uint8_t RGB_PWM_RESOLUTION_BITS = 8;
constexpr uint8_t RGB_FADE_STEP = 5;
constexpr uint32_t RGB_FADE_INTERVAL_MS = 20;
constexpr uint32_t STATUS_HOLD_MS = 700;
constexpr uint8_t LCD_I2C_ADDRESS = 0x27;
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
// Mot phut/request du de cap nhat output ma khong gay tai khong can thiet cho server.
constexpr uint32_t ANALYZE_INTERVAL_MS = 60000;
constexpr uint32_t DEMO_SCENARIO_INTERVAL_MS = 15000;
constexpr bool USE_VIRTUAL_LLM_DEMO = true;
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
FirebaseGateway firebaseGateway(FIREBASE_URL, FIREBASE_AUTH, DEVICE_ID);
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);


class AdviceDisplay {
 public:
  void begin() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    lcd.init();
    lcd.backlight();
    showStatus("BOOTING ESP32", "Please wait...");
  }

  void showStatus(const String &line1, const String &line2) {
    // Stop scrolling the previous advice while a connection status is displayed.
    title_ = line1;
    message_ = "";
    scrollOffset_ = 0;
    lastScrollMs_ = 0;
    writeLine(0, line1);
    writeLine(1, line2);
  }

  void apply(const ServerAnalysis &analysis) {
    title_ = analysis.riskLevel + " " + analysis.hazard;
    message_ = asciiForLcd(analysis.advice);
    if (message_.length() == 0) message_ = "No advice";
    scrollOffset_ = 0;
    lastScrollMs_ = 0;
    writeLine(0, title_);
    renderMessage();
  }

  void showScenario(const char *scenarioName) {
    showStatus("Preparing data", scenarioName);
  }

  void update() {
    if (message_.length() <= LCD_COLUMNS) return;
    const uint32_t now = millis();
    if (lastScrollMs_ == 0 || now - lastScrollMs_ >= 180) {
      lastScrollMs_ = now;
      scrollOffset_ = (scrollOffset_ + 1) % (message_.length() + LCD_COLUMNS);
      renderMessage();
    }
  }

 private:
  String asciiForLcd(const String &value) {
    String result;
    result.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
      const uint8_t byte = static_cast<uint8_t>(value[i]);
      if (byte >= 32 && byte <= 126) result += static_cast<char>(byte);
      else if ((byte & 0xC0) != 0x80) result += '?';
    }
    return result;
  }

  void writeLine(uint8_t row, const String &text) {
    lcd.setCursor(0, row);
    String padded = text.substring(0, LCD_COLUMNS);
    while (padded.length() < LCD_COLUMNS) padded += ' ';
    lcd.print(padded);
  }

  void renderMessage() {
    if (message_.length() <= LCD_COLUMNS) {
      writeLine(1, message_);
      return;
    }
    String loopText = message_;
    for (uint8_t i = 0; i < LCD_COLUMNS; ++i) loopText += ' ';
    String window;
    window.reserve(LCD_COLUMNS);
    for (uint8_t i = 0; i < LCD_COLUMNS; ++i) {
      window += loopText[(scrollOffset_ + i) % loopText.length()];
    }
    writeLine(1, window);
  }

  String title_;
  String message_;
  size_t scrollOffset_ = 0;
  uint32_t lastScrollMs_ = 0;
};

AdviceDisplay display;

enum class BuzzerState { OFF, BEEP, URGENT_BEEP };

class OutputController {
 public:
  void begin() {
    ledcSetup(RGB_RED_PWM_CHANNEL, RGB_PWM_FREQUENCY_HZ, RGB_PWM_RESOLUTION_BITS);
    ledcSetup(RGB_GREEN_PWM_CHANNEL, RGB_PWM_FREQUENCY_HZ, RGB_PWM_RESOLUTION_BITS);
    ledcSetup(RGB_BLUE_PWM_CHANNEL, RGB_PWM_FREQUENCY_HZ, RGB_PWM_RESOLUTION_BITS);
    ledcAttachPin(RGB_RED_PIN, RGB_RED_PWM_CHANNEL);
    ledcAttachPin(RGB_GREEN_PIN, RGB_GREEN_PWM_CHANNEL);
    ledcAttachPin(RGB_BLUE_PIN, RGB_BLUE_PWM_CHANNEL);
    pinMode(BUZZER_PIN, OUTPUT);
    currentRed_ = targetRed_ = 0;
    currentGreen_ = targetGreen_ = 0;
    currentBlue_ = targetBlue_ = 255;  // Blue means waiting/unknown.
    writeRgb();
    digitalWrite(BUZZER_PIN, LOW);
  }

  void apply(const ServerAnalysis &analysis) {
    const uint8_t confidence = constrain(analysis.confidencePercent, 0, 100);
    if (analysis.ledColor == "GREEN") {
      setRgbTarget(0, 255, 0);
    } else if (analysis.ledColor == "YELLOW") {
      // Higher-confidence warnings move from yellow toward orange.
      setRgbTarget(128 + confidence * 127 / 100,
                   255 - confidence * 100 / 100, 0);
    } else if (analysis.ledColor == "RED") {
      // Higher-confidence critical alerts move from orange-red to pure red.
      setRgbTarget(255, (100 - confidence) * 2, 0);
    } else {
      setRgbTarget(0, 0, 255);
    }
    Serial.printf("RGB TARGET   | R=%u G=%u B=%u | fade~1s\n",
                  targetRed_, targetGreen_, targetBlue_);

    if (analysis.buzzerMode == "URGENT_BEEP") buzzerState_ = BuzzerState::URGENT_BEEP;
    else if (analysis.buzzerMode == "BEEP") buzzerState_ = BuzzerState::BEEP;
    else buzzerState_ = BuzzerState::OFF;
    lastToggleMs_ = millis();
    lastLedToggleMs_ = millis();
    buzzerOn_ = buzzerState_ != BuzzerState::OFF;
    ledOn_ = true;
    writeRgb();
    digitalWrite(BUZZER_PIN, buzzerOn_ ? HIGH : LOW);
  }

  void update() {
    updateRgbFade();
    updateLedBlink();
    if (buzzerState_ == BuzzerState::OFF) {
      digitalWrite(BUZZER_PIN, LOW);
      return;
    }
    const uint32_t now = millis();
    // Both alert levels use a repeating intermittent pattern at full GPIO level.
    // CRITICAL uses longer ON and shorter OFF periods so it sounds more urgent.
    const uint32_t onDuration = buzzerState_ == BuzzerState::URGENT_BEEP ? 500 : 300;
    const uint32_t offDuration = buzzerState_ == BuzzerState::URGENT_BEEP ? 150 : 500;
    const uint32_t duration = buzzerOn_ ? onDuration : offDuration;
    if (now - lastToggleMs_ >= duration) {
      buzzerOn_ = !buzzerOn_;
      lastToggleMs_ = now;
      digitalWrite(BUZZER_PIN, buzzerOn_ ? HIGH : LOW);
    }
  }

 private:
  static uint8_t approach(uint8_t current, uint8_t target) {
    if (current < target) return min<int>(current + RGB_FADE_STEP, target);
    if (current > target) return max<int>(current - RGB_FADE_STEP, target);
    return current;
  }

  void setRgbTarget(uint8_t red, uint8_t green, uint8_t blue) {
    targetRed_ = red;
    targetGreen_ = green;
    targetBlue_ = blue;
  }

  void updateRgbFade() {
    const uint32_t now = millis();
    if (now - lastFadeMs_ < RGB_FADE_INTERVAL_MS) return;
    lastFadeMs_ = now;
    currentRed_ = approach(currentRed_, targetRed_);
    currentGreen_ = approach(currentGreen_, targetGreen_);
    currentBlue_ = approach(currentBlue_, targetBlue_);
    writeRgb();
  }

  void updateLedBlink() {
    if (buzzerState_ == BuzzerState::OFF) {
      if (!ledOn_) {
        ledOn_ = true;
        writeRgb();
      }
      return;
    }
    const uint32_t now = millis();
    const uint32_t onDuration = buzzerState_ == BuzzerState::URGENT_BEEP ? 200 : 600;
    const uint32_t offDuration = buzzerState_ == BuzzerState::URGENT_BEEP ? 120 : 400;
    const uint32_t duration = ledOn_ ? onDuration : offDuration;
    if (now - lastLedToggleMs_ >= duration) {
      ledOn_ = !ledOn_;
      lastLedToggleMs_ = now;
      writeRgb();
    }
  }

  void writeRgb() {
    // Common anode: duty 255 is OFF and duty 0 is maximum brightness.
    const uint8_t red = ledOn_ ? currentRed_ : 0;
    const uint8_t green = ledOn_ ? currentGreen_ : 0;
    const uint8_t blue = ledOn_ ? currentBlue_ : 0;
    ledcWrite(RGB_RED_PWM_CHANNEL, 255 - red);
    ledcWrite(RGB_GREEN_PWM_CHANNEL, 255 - green);
    ledcWrite(RGB_BLUE_PWM_CHANNEL, 255 - blue);
  }

  BuzzerState buzzerState_ = BuzzerState::OFF;
  bool buzzerOn_ = false;
  bool ledOn_ = true;
  uint32_t lastToggleMs_ = 0;
  uint32_t lastLedToggleMs_ = 0;
  uint32_t lastFadeMs_ = 0;
  uint8_t currentRed_ = 0;
  uint8_t currentGreen_ = 0;
  uint8_t currentBlue_ = 0;
  uint8_t targetRed_ = 0;
  uint8_t targetGreen_ = 0;
  uint8_t targetBlue_ = 0;
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

enum class DemoScenario : uint8_t { EARTHQUAKE, FLOOD, BLIZZARD };

constexpr DemoScenario DEMO_SCENARIOS[] = {
    DemoScenario::EARTHQUAKE,
    DemoScenario::FLOOD,
    DemoScenario::BLIZZARD,
};
constexpr size_t DEMO_SCENARIO_COUNT = sizeof(DEMO_SCENARIOS) / sizeof(DEMO_SCENARIOS[0]);

const char *demoScenarioName(DemoScenario scenario) {
  switch (scenario) {
    case DemoScenario::EARTHQUAKE: return "EARTHQUAKE";
    case DemoScenario::FLOOD: return "FLOOD";
    case DemoScenario::BLIZZARD: return "BLIZZARD";
  }
  return "UNKNOWN";
}

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

String buildDemoPayload(DemoScenario scenario) {
  JsonDocument document;
  document["device_id"] = DEVICE_ID;
  document["timestamp_ms"] = millis();
  document["dht11"]["valid"] = true;
  document["dht11"]["temperature_c"] = 27.0F;
  document["dht11"]["humidity_percent"] = 55.0F;
  document["lm35"]["valid"] = true;
  document["lm35"]["temperature_c"] = 27.5F;
  document["hc_sr04"]["valid"] = true;
  document["hc_sr04"]["echo_time_us"] = 4100;
  document["hc_sr04"]["distance_cm"] = 70.0F;
  document["hc_sr04"]["water_height_cm"] = 30.0F;
  document["steam_sensor"]["valid"] = true;
  document["steam_sensor"]["adc_raw"] = 600;
  document["steam_sensor"]["wet_percent"] = 20.0F;
  document["water_sensor"]["valid"] = true;
  document["water_sensor"]["adc_raw"] = 750;
  document["water_sensor"]["level_percent"] = 25.0F;
  document["ks0272_vibration"]["valid"] = true;
  document["ks0272_vibration"]["current_raw"] = 1500;
  document["ks0272_vibration"]["min_raw"] = 1460;
  document["ks0272_vibration"]["max_raw"] = 1540;
  document["ks0272_vibration"]["peak_to_peak_raw"] = 80;
  document["ks0272_vibration"]["mean_raw"] = 1500.0F;
  document["ks0272_vibration"]["rms_raw"] = 18.0F;
  document["ks0272_vibration"]["event_count"] = 0;
  document["ks0272_vibration"]["saturated"] = false;

  switch (scenario) {
    case DemoScenario::EARTHQUAKE:
      document["ks0272_vibration"]["current_raw"] = 2300;
      document["ks0272_vibration"]["min_raw"] = 500;
      document["ks0272_vibration"]["max_raw"] = 3300;
      document["ks0272_vibration"]["peak_to_peak_raw"] = 2800;
      document["ks0272_vibration"]["mean_raw"] = 1550.0F;
      document["ks0272_vibration"]["rms_raw"] = 480.0F;
      document["ks0272_vibration"]["event_count"] = 42;
      break;
    case DemoScenario::FLOOD:
      document["dht11"]["humidity_percent"] = 96.0F;
      document["hc_sr04"]["echo_time_us"] = 580;
      document["hc_sr04"]["distance_cm"] = 10.0F;
      document["hc_sr04"]["water_height_cm"] = 90.0F;
      document["steam_sensor"]["adc_raw"] = 2850;
      document["steam_sensor"]["wet_percent"] = 95.0F;
      document["water_sensor"]["adc_raw"] = 2850;
      document["water_sensor"]["level_percent"] = 95.0F;
      break;
    case DemoScenario::BLIZZARD:
      document["dht11"]["temperature_c"] = -12.0F;
      document["dht11"]["humidity_percent"] = 92.0F;
      document["lm35"]["temperature_c"] = -11.5F;
      document["steam_sensor"]["adc_raw"] = 1800;
      document["steam_sensor"]["wet_percent"] = 60.0F;
      document["water_sensor"]["adc_raw"] = 300;
      document["water_sensor"]["level_percent"] = 10.0F;
      break;
  }
  document["all_sensors_valid"] = true;

  String payload;
  serializeJson(document, payload);
  return payload;
}

void printReport(const SensorData &data, const String &payload) {
  Serial.println();
  Serial.println("============================================================");
  Serial.printf("SENSOR REPORT | uptime: %lu ms\n", millis());
  Serial.println("------------------------------------------------------------");

  Serial.print("DHT11      | Temperature: ");
  printFloatOrError(data.dhtTemperatureC);
  Serial.print(" C | Humidity: ");
  printFloatOrError(data.humidityPercent);
  Serial.printf(" %% | %s\n", data.dhtValid ? "OK" : "READ ERROR");

  Serial.print("LM35       | Temperature: ");
  printFloatOrError(data.lm35TemperatureC);
  Serial.printf(" C | 32-sample ADC average | %s\n", data.lm35Valid ? "OK" : "READ ERROR");

  Serial.printf("HC-SR04    | Echo: %lu us | Distance: ", data.echoTimeUs);
  printFloatOrError(data.ultrasonicDistanceCm);
  Serial.print(" cm | Water height: ");
  printFloatOrError(data.waterHeightCm);
  Serial.printf(" cm | %s\n", data.ultrasonicValid ? "OK" : "TIMEOUT/ERROR");

  Serial.printf("STEAM/RAIN | ADC raw: %d/4095 | Wet level: %.1f %% | %s\n",
                data.steamRaw, data.steamPercent, data.steamValid ? "OK" : "RAIL STUCK");
  Serial.printf("WATER      | ADC raw: %d/4095 | Water level: %.1f %% | %s\n",
                data.waterRaw, data.waterPercent, data.waterValid ? "OK" : "RAIL STUCK");

  Serial.printf("KS0272     | Raw: %d | Min/Max: %d/%d | Peak-to-peak: %d\n",
                data.vibrationCurrentRaw, data.vibrationMinRaw,
                data.vibrationMaxRaw, data.vibrationPeakToPeak);
  Serial.printf("            | Mean: %.2f | RMS: %.2f | Events: %u | Saturated: %s | %s\n",
                data.vibrationMeanRaw, data.vibrationRmsRaw,
                data.vibrationEventCount, data.vibrationSaturated ? "YES" : "NO",
                data.vibrationValid ? "OK" : "READ ERROR");

  Serial.println("------------------------------------------------------------");
  Serial.printf("STATUS      | All sensors: %s\n",
                allSensorsValid(data) ? "READ SUCCESS" : "SENSOR ERROR DETECTED");
  if (!data.dhtValid) Serial.println("RECOMMEND   | Check the DHT11 DATA wire and power.");
  if (!data.lm35Valid) Serial.println("RECOMMEND   | Check LM35 OUT/power and calibration.");
  if (!data.ultrasonicValid) Serial.println("RECOMMEND   | Check HC-SR04 and the ECHO voltage divider.");
  if (!data.steamValid) Serial.println("RECOMMEND   | STEAM is at the high ADC rail; check AO/DO and signal voltage.");
  if (!data.waterValid) Serial.println("RECOMMEND   | WATER is at the high ADC rail; check AO/DO and signal voltage.");
  if (!data.vibrationValid) Serial.println("RECOMMEND   | Check the KS0272 signal wire and power.");
  if (data.vibrationSaturated) Serial.println("RECOMMEND   | KS0272 reached ADC 4095; vibration signal is clipping.");
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

bool firebaseConfigReady() {
  return strlen(FIREBASE_URL) > 0 &&
         strcmp(FIREBASE_URL, "https://your-project-id.firebaseio.com") != 0;
}


void connectWifi() {
  if (!deviceConfigReady()) {
    Serial.println("HTTP disabled: device_secrets.h is missing or still has placeholder values.");
    Serial.println("Copy include/device_secrets.example.h to include/device_secrets.h and edit it.");
    display.showStatus("CONFIG ERROR", "Check secrets");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(DEVICE_WIFI_SSID, DEVICE_WIFI_PASSWORD);
  display.showStatus("Connecting WiFi", DEVICE_WIFI_SSID);
  Serial.printf("SYSTEM STATUS | Connecting to WiFi: %s", DEVICE_WIFI_SSID);
  for (uint8_t i = 0; i < 20 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    const String ipAddress = WiFi.localIP().toString();
    Serial.printf(" connected, IP: %s\n", ipAddress.c_str());
    display.showStatus("WiFi connected", ipAddress);
    delay(STATUS_HOLD_MS);
    display.showStatus("Gemini API", "Ready to connect");
  } else {
    Serial.println(" not connected; firmware will retry.");
    display.showStatus("WiFi failed", "Retry pending");
  }
}

bool requestAnalysis(const String &payload, const char *scenarioName) {
  ServerAnalysis analysis;
  String error;
  display.showScenario(scenarioName);
  delay(250);
  display.showStatus("Connecting API", "Gemini...");
  Serial.println("API STATUS    | Connecting to Gemini through backend");
  Serial.printf("POST %s\n", DEVICE_SERVER_URL);
  if (!gateway.analyze(payload, analysis, error)) {
    Serial.printf("SERVER ERROR | %s\n", error.c_str());
    Serial.println("API STATUS    | Gemini connection failed; retry pending");
    display.showStatus("API failed", "Retry pending");
    return false;  // Keep the last known safe output state.
  }

  Serial.println("API STATUS    | Gemini response received successfully");
  display.showStatus("API connected", "Gemini online");
  delay(STATUS_HOLD_MS);

  Serial.printf("SERVER RESULT | risk=%s | hazard=%s | confidence=%d%%\n",
                analysis.riskLevel.c_str(), analysis.hazard.c_str(),
                analysis.confidencePercent);
  Serial.printf("ADVICE        | %s\n", analysis.advice.c_str());
  Serial.printf("REASON        | %s\n", analysis.reason.c_str());
  Serial.printf("OUTPUTS       | LED=%s | buzzer=%s\n",
                analysis.ledColor.c_str(), analysis.buzzerMode.c_str());
  outputs.apply(analysis);
  display.apply(analysis);
  return true;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  display.begin();
  Serial.println("SYSTEM STATUS | ESP32 boot started");
  delay(STATUS_HOLD_MS);
  display.showStatus("Initializing", "Sensors...");
  Serial.println("SYSTEM STATUS | Initializing sensors");

  pinMode(HC_TRIG_PIN, OUTPUT);
  pinMode(HC_ECHO_PIN, INPUT);
  digitalWrite(HC_TRIG_PIN, LOW);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH); // Phat coi buzzer lien tuc

  analogReadResolution(12);
  analogSetPinAttenuation(LM35_PIN, ADC_11db);
  analogSetPinAttenuation(STEAM_PIN, ADC_11db);
  analogSetPinAttenuation(WATER_PIN, ADC_11db);
  analogSetPinAttenuation(VIBRATION_PIN, ADC_11db);

  dht.begin();
  outputs.begin();
  display.showStatus("Outputs ready", "RGB + buzzer");
  Serial.println("SYSTEM STATUS | Sensors and outputs initialized");
  delay(STATUS_HOLD_MS);
  connectWifi();

  Serial.println("\nESP32 MULTI-HAZARD SENSOR NODE");
  Serial.printf("Device ID: %s\n", DEVICE_ID);
  Serial.printf("Serial: %lu baud | Sample period: %lu ms\n", SERIAL_BAUD, SAMPLE_INTERVAL_MS);
  Serial.printf("KS0272: GPIO %u | %u samples/window | threshold: %.0f ADC\n",
                VIBRATION_PIN, VIBRATION_SAMPLE_COUNT, VIBRATION_EVENT_THRESHOLD_ADC);
  Serial.printf("LLM demo: %s | %u scenarios | sent once per boot\n",
                USE_VIRTUAL_LLM_DEMO ? "ON" : "OFF", DEMO_SCENARIO_COUNT);
  Serial.println("NOTE: Calibrate all *_RAW constants and SENSOR_TO_BOTTOM_CM.");
}

void loop() {
  static uint32_t lastSampleMs = 0;
  static uint32_t lastAnalyzeMs = 0;
  static uint32_t lastWifiRetryMs = 0;
  static size_t demoScenarioIndex = 0;
  static bool demoCompleteReported = false;
  static bool wifiWasConnected = WiFi.status() == WL_CONNECTED;
  outputs.update();
  display.update();

  const uint32_t now = millis();
  if (deviceConfigReady() && WiFi.status() != WL_CONNECTED &&
      now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
    lastWifiRetryMs = now;
    display.showStatus("Reconnecting WiFi", DEVICE_WIFI_SSID);
    Serial.println("SYSTEM STATUS | Retrying WiFi connection");
    WiFi.reconnect();
  }
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected != wifiWasConnected) {
    wifiWasConnected = wifiConnected;
    if (wifiConnected) {
      const String ipAddress = WiFi.localIP().toString();
      Serial.printf("SYSTEM STATUS | WiFi restored, IP: %s\n", ipAddress.c_str());
      display.showStatus("WiFi restored", ipAddress);
    } else {
      Serial.println("SYSTEM STATUS | WiFi connection lost");
      display.showStatus("WiFi lost", "Retry pending");
    }
  }
  if (lastSampleMs == 0 || now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    const SensorData data = readAllSensors();
    const String payload = buildJsonPayload(data);
    printReport(data, payload);
    if (WiFi.status() == WL_CONNECTED) {
      if (firebaseConfigReady()) {
        String fbError;
        if (firebaseGateway.pushTelemetry(payload, fbError)) {
          Serial.println("FIREBASE RTDB | Telemetry pushed (overwritten latest data)");
        } else {
          Serial.printf("FIREBASE ERROR| %s\n", fbError.c_str());
        }
      }
      if (USE_VIRTUAL_LLM_DEMO && demoScenarioIndex < DEMO_SCENARIO_COUNT &&
          (lastAnalyzeMs == 0 || now - lastAnalyzeMs >= DEMO_SCENARIO_INTERVAL_MS)) {

        lastAnalyzeMs = now;
        const DemoScenario scenario = DEMO_SCENARIOS[demoScenarioIndex];
        const String demoPayload = buildDemoPayload(scenario);
        Serial.printf("DEMO SCENARIO | %s\n", demoScenarioName(scenario));
        Serial.print("DEMO_DATA: ");
        Serial.println(demoPayload);
        if (requestAnalysis(demoPayload, demoScenarioName(scenario))) ++demoScenarioIndex;
      } else if (!USE_VIRTUAL_LLM_DEMO &&
                 (lastAnalyzeMs == 0 || now - lastAnalyzeMs >= ANALYZE_INTERVAL_MS)) {
        lastAnalyzeMs = now;
        requestAnalysis(payload, "REAL SENSORS");
      } else if (USE_VIRTUAL_LLM_DEMO && demoScenarioIndex >= DEMO_SCENARIO_COUNT &&
                 !demoCompleteReported) {
        demoCompleteReported = true;
        Serial.println("LLM DEMO COMPLETE | Reset ESP32 to run all 3 scenarios again.");
      }
    }
  }
}

