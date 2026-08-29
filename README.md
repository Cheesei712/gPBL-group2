# gPBL Multi-Hazard Sensor Node

ESP32 reads six sensor groups, sends JSON via HTTP to FastAPI, receives `advice` and
`outputs`, and then controls the RGB LED and buzzer. The `backend/` directory includes a Python simulator to test the entire flow before connecting the physical ESP32.

## 1. RGB and Buzzer Wiring Diagram

Keep the sensor pin definitions specified at the top of `src/main.cpp`, then wire the following:

| Device | ESP32 Pin |
|---|---:|
| LCD I2C SDA | GPIO 21 |
| LCD I2C SCL | GPIO 22 |
| RGB Red via 220–330 Ω resistor | GPIO 25 |
| RGB Green via 220–330 Ω resistor | GPIO 33 |
| RGB Blue via 220–330 Ω resistor | GPIO 27 |
| Common RGB VCC/anode pin | 3.3V |
| Active buzzer signal | GPIO 18 |
| Buzzer GND | GND |

The default code is configured for RGB common-VCC/common-anode (active-low), LCD I2C 16x2
address `0x27`, and an active buzzer. Since GPIO21/22 are reserved for I2C and GPIO33 for RGB,
HC-SR04 TRIG is moved to GPIO23, and KS0272 is moved to GPIO36. If the buzzer requires higher current than the GPIO pin capacity, drive it via a transistor instead of directly from the GPIO pin.

Fixed backend output rules:

| Risk | RGB | Buzzer |
|---|---|---|
| `NORMAL` | Green | Off |
| `WARNING` | Yellow | Beep 200 ms per second |
| `CRITICAL` | Red | Continuous |
| `UNKNOWN` | Blue | Off |

RGB uses PWM and fades for about one second between states. Green means safe,
yellow gradually becomes orange for warnings, and orange gradually becomes red
for critical danger. The exact shade also follows the LLM `confidence_percent`.

## 2. Backend Setup

Requires Python 3.11+ and `uv`. From PowerShell in the project directory:

```powershell
cd backend
uv sync --extra dev
Copy-Item .env.example .env
```

Open `backend/.env` and use the initial test configuration:

```dotenv
GEMINI_API_KEY=
DEVICE_API_KEY=test-device-key
ANALYSIS_MODE=rules
LOG_LEVEL=INFO
```

`rules` mode provides deterministic results, generating `risk`, `advice`, and RGB/buzzer commands without calling the Internet/Gemini API, thus consuming no tokens. This is also the safe default mode if environment variables are missing. Do not commit the `.env` file to Git.

## 3. Running and Verifying the Server

In the first terminal window, inside `backend/`:

```powershell
uv run uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

In a second terminal window:

```powershell
Invoke-RestMethod http://127.0.0.1:8000/health
```

The expected response contains `status: ok` and `analysis_mode: rules`. Seeing `rules` confirms the system is using the local rule analyzer instead of calling Gemini. Swagger UI is available at `http://127.0.0.1:8000/docs`.

## 4. Sending Simulated Data with Python Simulator

From the second terminal, inside `backend/`, run each scenario:

```powershell
uv run python simulator.py --scenario normal --device-key test-device-key
uv run python simulator.py --scenario rain --device-key test-device-key
uv run python simulator.py --scenario flood --device-key test-device-key
uv run python simulator.py --scenario vibration --device-key test-device-key
uv run python simulator.py --scenario earthquake --device-key test-device-key
uv run python simulator.py --scenario blizzard --device-key test-device-key
uv run python simulator.py --scenario compound --device-key test-device-key
uv run python simulator.py --scenario sensor_error --device-key test-device-key
```

The simulator prints the transmitted telemetry alongside the received `risk_level`, `hazard`, `advice`, LED color, and buzzer mode. To send 5 repeating samples at 2-second intervals:

```powershell
uv run python simulator.py --scenario rain --count 5 --interval 2 --device-key test-device-key
```

To only generate and print JSON without making a server request:

```powershell
uv run python simulator.py --scenario flood --print-only
```

## 5. Configuring ESP32 to Call the Server

The ESP32 and the backend host machine must be connected to the same Wi-Fi network. On the backend host machine, find the IPv4 address:

```powershell
ipconfig
```

Copy the example configuration file:

```powershell
cd ..
Copy-Item include/device_secrets.example.h include/device_secrets.h
```

Open `include/device_secrets.h` and update the values. Do not use `127.0.0.1` because on ESP32 that address points to the ESP32 itself:

```cpp
#define DEVICE_ID "esp32-node-01"
#define DEVICE_WIFI_SSID "your-wifi-ssid"
#define DEVICE_WIFI_PASSWORD "your-wifi-password"
#define DEVICE_SERVER_URL "http://192.168.1.10:8000/api/v1/analyze"
#define DEVICE_API_KEY "test-device-key"
```

Replace `192.168.1.10` with the IPv4 address of your backend host machine. If the ESP32 cannot connect, allow TCP port 8000 through Windows Firewall on the Private network profile.

## 6. Building, Flashing, and Monitoring Firmware

From the project root directory:

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

Every 2 seconds, the firmware reads and prints real sensor values. In default demo mode, the firmware sequentially sends exactly three simulated payloads (`EARTHQUAKE`, `FLOOD`, `BLIZZARD`) 15 seconds apart, then pauses LLM requests to conserve API quota. The Serial Monitor will display lines such as:

```text
SERVER RESULT | risk=WARNING | hazard=HEAVY_RAIN | confidence=85%
ADVICE        | Monitor water levels and avoid low-lying areas.
OUTPUTS       | LED=YELLOW | buzzer=BEEP
```

If an HTTP error occurs, the firmware prints `SERVER ERROR` and retains the last valid output state. When no server response is available yet, a blue LED indicates an unknown state.

## 7. Switching from Rules Mode to Gemini

Once the simulator and ESP32 workflow are verified and stable, update `backend/.env`:

```dotenv
GEMINI_API_KEY=your-real-gemini-api-key
DEVICE_API_KEY=your-long-random-device-key
ANALYSIS_MODE=gemini
```

Update the matching `DEVICE_API_KEY` in `include/device_secrets.h`, restart the backend server, and re-flash the firmware. The Gemini API key remains strictly on the backend and is never stored on the ESP32.

## 8. Running the Full Test Suite

Backend:

```powershell
cd backend
uv run ruff check .
uv run pytest -q
```

Firmware:

```powershell
cd ..
pio run
```
