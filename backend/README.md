# gPBL Disaster LLM Backend

Backend nhận telemetry JSON từ ESP32, xác thực schema, gọi Gemini bằng structured output
và trả về lời khuyên cùng lệnh LED/buzzer đã được ánh xạ cố định.

## Luồng dữ liệu

```text
ESP32 sensors -> POST /api/v1/analyze -> FastAPI -> Gemini
ESP32 outputs <- validated JSON       <- FastAPI <- structured response
```

Gemini API key chỉ nằm ở backend. ESP32 chỉ giữ `DEVICE_API_KEY` để gọi endpoint.

## Cài đặt

Yêu cầu Python 3.11 trở lên.

```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -e ".[dev]"
Copy-Item .env.example .env
```

Điền `GEMINI_API_KEY` và một `DEVICE_API_KEY` dài, ngẫu nhiên vào `.env`, sau đó chạy:

```powershell
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Swagger UI: `http://localhost:8000/docs`

## Gọi thử endpoint

```powershell
$headers = @{ "X-Device-Key" = "your-device-key" }
$body = Get-Content -Raw .\sample-telemetry.json
Invoke-RestMethod -Method Post `
  -Uri "http://localhost:8000/api/v1/analyze" `
  -Headers $headers `
  -ContentType "application/json" `
  -Body $body
```

## Quy tắc phần cứng

LLM không tự tạo lệnh GPIO. Backend ánh xạ cố định:

| Risk | LED | Buzzer |
|---|---|---|
| NORMAL | GREEN | OFF |
| WARNING | YELLOW | BEEP |
| CRITICAL | RED | CONTINUOUS |
| UNKNOWN | BLUE | OFF |

## Kiểm thử

Test không gọi Gemini thật:

```powershell
pytest
ruff check .
```

## Docker

```powershell
docker build -t gpbl-llm-backend .
docker run --rm -p 8000:8000 --env-file .env gpbl-llm-backend
```

