# gPBL multi-hazard sensor node

ESP32 đọc sáu nhóm cảm biến, gửi JSON qua HTTP đến FastAPI, nhận `advice` và
`outputs`, sau đó điều khiển LED RGB và buzzer. Thư mục `backend/` có thêm trình mô
phỏng Python để kiểm tra toàn bộ luồng trước khi cắm ESP32.

## 1. Sơ đồ nối thêm RGB và buzzer

Giữ nguyên các chân cảm biến được ghi ở đầu `src/main.cpp`, sau đó nối:

| Thiết bị | Chân ESP32 |
|---|---:|
| LCD I2C SDA | GPIO 21 |
| LCD I2C SCL | GPIO 22 |
| RGB Red qua điện trở 220–330 Ω | GPIO 25 |
| RGB Green qua điện trở 220–330 Ω | GPIO 33 |
| RGB Blue qua điện trở 220–330 Ω | GPIO 27 |
| Chân chung RGB VCC/anode | 3.3V |
| Active buzzer signal | GPIO 18 |
| Buzzer GND | GND |

Code mặc định dành cho RGB common-VCC/common-anode (active-low), LCD I2C 16x2
địa chỉ `0x27` và active buzzer. Vì GPIO21/22 dành cho I2C và GPIO33 dành cho RGB,
HC-SR04 TRIG được chuyển sang GPIO23, KS0272 được chuyển sang GPIO36. Nếu buzzer cần dòng
lớn hơn khả năng GPIO, phải điều khiển qua transistor, không cấp trực tiếp từ chân GPIO.

Quy tắc output cố định ở backend:

| Risk | RGB | Buzzer |
|---|---|---|
| `NORMAL` | Green | Off |
| `WARNING` | Yellow | Beep 200 ms mỗi giây |
| `CRITICAL` | Red | Continuous |
| `UNKNOWN` | Blue | Off |

RGB uses PWM and fades for about one second between states. Green means safe,
yellow gradually becomes orange for warnings, and orange gradually becomes red
for critical danger. The exact shade also follows the LLM `confidence_percent`.

## 2. Chuẩn bị backend

Yêu cầu Python 3.11+ và `uv`. Từ PowerShell tại thư mục dự án:

```powershell
cd backend
uv sync --extra dev
Copy-Item .env.example .env
```

Mở `backend/.env` và dùng cấu hình thử nghiệm trước:

```dotenv
GEMINI_API_KEY=
DEVICE_API_KEY=test-device-key
ANALYSIS_MODE=rules
LOG_LEVEL=INFO
```

`rules` cho kết quả xác định, vẫn sinh `risk`, `advice` và lệnh RGB/buzzer nhưng
không gọi Internet/Gemini, vì vậy không tiêu thụ token. Đây cũng là chế độ mặc định
an toàn nếu biến môi trường bị thiếu. Không đưa file `.env` lên Git.

## 3. Chạy và kiểm tra server

Trong terminal thứ nhất, vẫn ở `backend/`:

```powershell
uv run uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Mở terminal thứ hai:

```powershell
Invoke-RestMethod http://127.0.0.1:8000/health
```

Kết quả mong đợi chứa `status: ok` và `analysis_mode: rules`. Khi thấy `rules`, hệ
thống chắc chắn đang dùng bộ phân tích cục bộ thay vì gọi Gemini. Swagger UI nằm tại
`http://127.0.0.1:8000/docs`.

## 4. Gửi dữ liệu ảo bằng Python simulator

Từ terminal thứ hai, trong `backend/`, chạy từng kịch bản:

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

Simulator in telemetry gửi đi, `risk_level`, `hazard`, `advice`, màu LED và chế độ
buzzer nhận về. Để gửi lặp lại năm mẫu, cách nhau hai giây:

```powershell
uv run python simulator.py --scenario rain --count 5 --interval 2 --device-key test-device-key
```

Chỉ sinh và xem JSON, không gọi server:

```powershell
uv run python simulator.py --scenario flood --print-only
```

## 5. Cấu hình ESP32 gọi server

ESP32 và máy chạy backend phải ở cùng mạng Wi-Fi. Trên máy chạy backend, tìm IPv4:

```powershell
ipconfig
```

Sao chép file cấu hình mẫu:

```powershell
cd ..
Copy-Item include/device_secrets.example.h include/device_secrets.h
```

Mở `include/device_secrets.h` và thay ba giá trị. Không dùng `127.0.0.1` vì trên
ESP32 địa chỉ đó trỏ về chính ESP32:

```cpp
#define DEVICE_ID "esp32-node-01"
#define DEVICE_WIFI_SSID "ten-wifi"
#define DEVICE_WIFI_PASSWORD "mat-khau-wifi"
#define DEVICE_SERVER_URL "http://192.168.1.10:8000/api/v1/analyze"
#define DEVICE_API_KEY "test-device-key"
```

Thay `192.168.1.10` bằng IPv4 của máy chạy backend. Nếu ESP32 không kết nối được,
cho phép TCP port 8000 qua Windows Firewall trên mạng Private.

## 6. Build, nạp và quan sát firmware

Tại thư mục gốc dự án:

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

Cứ 2 giây firmware đọc và in cảm biến thật. Ở chế độ demo mặc định, firmware gửi
lần lượt đúng ba payload ảo `EARTHQUAKE`, `FLOOD`, `BLIZZARD`, cách nhau 15 giây,
rồi dừng gọi LLM để tiết kiệm quota. Serial Monitor sẽ hiện các dòng dạng:

```text
SERVER RESULT | risk=WARNING | hazard=HEAVY_RAIN | confidence=85%
ADVICE        | Theo dõi mực nước và hạn chế đi qua khu vực trũng.
OUTPUTS       | LED=YELLOW | buzzer=BEEP
```

Nếu HTTP lỗi, firmware in `SERVER ERROR` và giữ output hợp lệ gần nhất. Khi chưa có
kết quả server, LED màu xanh dương biểu thị trạng thái chưa xác định.

## 7. Chuyển từ rules sang Gemini

Sau khi luồng simulator và ESP32 hoạt động ổn định, sửa `backend/.env`:

```dotenv
GEMINI_API_KEY=your-real-gemini-api-key
DEVICE_API_KEY=your-long-random-device-key
ANALYSIS_MODE=gemini
```

Cập nhật cùng `DEVICE_API_KEY` trong `include/device_secrets.h`, rồi khởi động lại
backend và nạp lại firmware. Khóa Gemini chỉ nằm trên backend, không đặt trên ESP32.

## 8. Chạy toàn bộ kiểm thử

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
