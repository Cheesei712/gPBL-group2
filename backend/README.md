# gPBL Disaster LLM Backend

The backend receives telemetry JSON from ESP32, validates the schema, calls Gemini using structured output, and returns advice alongside deterministically mapped LED/buzzer commands.

## Data Flow

```text
ESP32 sensors -> POST /api/v1/analyze -> FastAPI -> Gemini
ESP32 outputs <- validated JSON       <- FastAPI <- structured response
```

The Gemini API key is kept strictly on the backend. The ESP32 only holds `DEVICE_API_KEY` to authenticate API endpoint calls.

## Installation

Requires Python 3.11 or higher.

```powershell
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -e ".[dev]"
Copy-Item .env.example .env
```

Enter `GEMINI_API_KEY` and a long, random `DEVICE_API_KEY` in `.env`, then run:

```powershell
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Swagger UI: `http://localhost:8000/docs`

## Testing the Endpoint

```powershell
$headers = @{ "X-Device-Key" = "your-device-key" }
$body = Get-Content -Raw .\sample-telemetry.json
Invoke-RestMethod -Method Post `
  -Uri "http://localhost:8000/api/v1/analyze" `
  -Headers $headers `
  -ContentType "application/json" `
  -Body $body
```

## Hardware Rules

The LLM does not generate raw GPIO commands directly. The backend maps risk levels deterministically:

| Risk | LED | Buzzer |
|---|---|---|
| NORMAL | GREEN | OFF |
| WARNING | YELLOW | BEEP |
| CRITICAL | RED | URGENT_BEEP (500 ms on / 150 ms off) |
| UNKNOWN | BLUE | OFF |

## Testing

Run tests without calling the live Gemini API:

```powershell
pytest
ruff check .
```

## Docker

```powershell
docker build -t gpbl-llm-backend .
docker run --rm -p 8000:8000 --env-file .env gpbl-llm-backend
```
