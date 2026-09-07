# gPBL Multi-Hazard Sensor Node & Disaster Intelligence Gateway

An IoT disaster monitoring and early-warning edge computing platform. The **ESP32** continuously samples six environmental sensors, pushes real-time telemetry over HTTPS to **Firebase Realtime Database**, and communicates with a **FastAPI backend** powered by **Google Gemini AI** to assess hazards, output evacuation advice, and command hardware warning indicators (RGB LED and active buzzer).

---

## 1. Hardware Pinout & Wiring Diagram

The ESP32 Dev Module uses the pin assignments specified in `src/main.cpp`:

| Peripheral / Sensor | ESP32 Pin | Signal Type | Description |
|---|---|---|---|
| **LCD 16x2 I2C SDA** | GPIO 21 | Digital (I2C) | Shared I2C Data bus |
| **LCD 16x2 I2C SCL** | GPIO 22 | Digital (I2C) | Shared I2C Clock bus |
| **HC-SR04 TRIG** | GPIO 23 | Digital Output | 10 µs trigger pulse |
| **HC-SR04 ECHO** | GPIO 26 | Digital Input | **MANDATORY**: 5V to 3.3V voltage divider (e.g. 1kΩ / 2kΩ) |
| **LM35 Temperature OUT** | GPIO 34 | Analog (ADC1) | 10 mV/°C analog voltage (input only) |
| **Steam / Rain AO** | GPIO 35 | Analog (ADC1) | Analog rain/moisture level (input only) |
| **Water Level AO** | GPIO 32 | Analog (ADC1) | Submersion depth level |
| **KS0272 Vibration S** | GPIO 36 | Analog (ADC1) | Piezo vibration sensor (input only) |
| **RGB LED Red** | GPIO 27 | PWM Output | Connected via 220–330 Ω resistor |
| **RGB LED Green** | GPIO 25 | PWM Output | Connected via 220–330 Ω resistor |
| **RGB LED Blue** | GPIO 33 | PWM Output | Connected via 220–330 Ω resistor |

| **RGB Common Anode** | 3.3V / VCC | Power | Active-Low common anode configuration |
| **Active Buzzer** | GPIO 18 | Digital Output | Continuous and intermittent alarm signaling |

> **Note**: All sensors, modules, and the ESP32 must share a common **GND**.

---

## 2. Deterministic Warning & Output Mapping

To guarantee hardware safety, Gemini LLM does not generate arbitrary GPIO signals. Instead, the system uses strict deterministic mappings:

| Risk Level | RGB LED Color | Buzzer Alarm Mode | LCD Display Status |
|---|---|---|---|
| `NORMAL` | **Green** | `OFF` | Green status / Safe conditions |
| `WARNING` | **Yellow / Orange** | `BEEP` (300 ms on / 500 ms off) | Hazard warning with scrolling advice |
| `CRITICAL` | **Red** | `URGENT_BEEP` (500 ms on / 150 ms off) | Critical emergency alert with evacuation guidance |
| `UNKNOWN` | **Blue** | `OFF` | System standby / Connecting |

---

## 3. Backend Architecture & Setup

The backend requires **Python 3.11+** and **uv**.

### Step 1: Install Dependencies
```powershell
cd backend
uv sync --extra dev
Copy-Item .env.example .env
```

### Step 2: Configure Environment Variables (`backend/.env`)
```dotenv
# Direct Google AI Studio Gemini API Key
GEMINI_API_KEY=your-gemini-api-key
GEMINI_MODEL=gemini-3.5-flash-lite

# Device Authentication Key
DEVICE_API_KEY=19335ba33fdf49cba06119b4a3f09299

# Analysis Mode: "gemini", "openrouter", or "rules"
ANALYSIS_MODE=gemini
LOG_LEVEL=INFO

# Firebase Realtime Database Sync
FIREBASE_URL=https://gpbl-group2-default-rtdb.asia-southeast1.firebasedatabase.app
FIREBASE_AUTH=lh3H7engPhQxhBJdvJEg4smwjmx4YSBTZufSFSlQ
```

### Step 3: Start the Backend Server
```powershell
uv run uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

* **Swagger UI Documentation**: [http://127.0.0.1:8000/docs](http://127.0.0.1:8000/docs)
* **Healthcheck**: [http://127.0.0.1:8000/health](http://127.0.0.1:8000/health)

---

## 4. ESP32 Firmware Configuration & Flashing

### Step 1: Configure Device Secrets (`include/device_secrets.h`)
```cpp
#pragma once

#define DEVICE_ID "esp32-node-01"
#define DEVICE_WIFI_SSID "your-wifi-ssid"
#define DEVICE_WIFI_PASSWORD "your-wifi-password"
#define DEVICE_SERVER_URL "http://192.168.1.10:8000/api/v1/analyze"
#define DEVICE_API_KEY "19335ba33fdf49cba06119b4a3f09299"
#define FIREBASE_URL "https://gpbl-group2-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "lh3H7engPhQxhBJdvJEg4smwjmx4YSBTZufSFSlQ"
```

> **Important**: Replace `192.168.1.10` with the actual IPv4 address of your computer obtained via `ipconfig`.

### Step 2: Build & Flash Firmware
```powershell
pio run --target upload
pio device monitor --baud 115200
```

---

## 5. System Execution Cycle

1. **Physical Sensor Reading (Every 2 seconds)**:
   ESP32 samples all 6 hardware sensor groups and constructs a validated JSON telemetry packet.
2. **Firebase Realtime Database Push (Every 2 seconds)**:
   ESP32 uploads telemetry directly to `/devices/esp32-node-01/telemetry.json` via secure HTTPS PUT.
3. **AI Hazard Assessment (Every 60 seconds)**:
   ESP32 submits telemetry to the FastAPI backend (`POST /api/v1/analyze`). Gemini AI analyzes trends and returns structured assessments.
4. **Bidirectional Firebase Alert Listener (Every 3 seconds)**:
   ESP32 fetches `/devices/esp32-node-01/analysis.json` from Firebase. If a new emergency condition is identified, the ESP32 dynamically switches the RGB LED color, activates the buzzer cadence, and scrolls actionable safety advice on the LCD.

---

## 6. Testing & Simulation

To test the AI analysis pipeline without physical sensors:

```powershell
cd backend
uv run python simulator.py --scenario rain --device-key 19335ba33fdf49cba06119b4a3f09299
uv run python simulator.py --scenario flood --device-key 19335ba33fdf49cba06119b4a3f09299
uv run python simulator.py --scenario earthquake --device-key 19335ba33fdf49cba06119b4a3f09299
```

To run the automated backend test suite:
```powershell
uv run pytest -q
```
