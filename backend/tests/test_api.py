from fastapi.testclient import TestClient

from app.dependencies import get_analysis_service
from app.main import app
from app.models import (
    AnalysisResponse,
    BuzzerMode,
    Hazard,
    LedColor,
    OutputCommand,
    RiskLevel,
)


class FakeGeminiService:
    async def analyze(self, telemetry):  # noqa: ANN001
        return AnalysisResponse(
            device_id=telemetry.device_id,
            model="fake-model",
            risk_level=RiskLevel.WARNING,
            hazard=Hazard.HEAVY_RAIN,
            confidence_percent=82,
            advice="Theo dõi mực nước và tránh khu vực trũng.",
            reason="Độ ẩm và tín hiệu bề mặt ướt đều cao.",
            outputs=OutputCommand(led_color=LedColor.YELLOW, buzzer_mode=BuzzerMode.BEEP),
        )


TELEMETRY = {
    "device_id": "esp32-node-01",
    "timestamp_ms": 123456,
    "dht11": {"valid": True, "temperature_c": 31.2, "humidity_percent": 91.0},
    "lm35": {"valid": True, "temperature_c": 30.8},
    "hc_sr04": {
        "valid": True,
        "echo_time_us": 2500,
        "distance_cm": 43.2,
        "water_height_cm": 56.8,
    },
    "steam_sensor": {"valid": True, "adc_raw": 2100, "wet_percent": 70.0},
    "water_sensor": {"valid": True, "adc_raw": 1800, "level_percent": 60.0},
    "ks0272_vibration": {
        "valid": True,
        "current_raw": 1520,
        "min_raw": 1450,
        "max_raw": 1710,
        "peak_to_peak_raw": 260,
        "mean_raw": 1515.4,
        "rms_raw": 42.5,
        "event_count": 2,
        "saturated": False,
    },
    "all_sensors_valid": True,
}


def test_health() -> None:
    with TestClient(app) as client:
        response = client.get("/health")
    assert response.status_code == 200
    assert response.json() == {"status": "ok", "analysis_mode": "rules"}


def test_analyze_requires_device_key() -> None:
    with TestClient(app) as client:
        response = client.post("/api/v1/analyze", json=TELEMETRY)
    assert response.status_code == 401


def test_analyze_returns_validated_commands() -> None:
    app.dependency_overrides[get_analysis_service] = lambda: FakeGeminiService()
    try:
        with TestClient(app) as client:
            response = client.post(
                "/api/v1/analyze",
                json=TELEMETRY,
                headers={"X-Device-Key": "test-device-key"},
            )
    finally:
        app.dependency_overrides.clear()

    assert response.status_code == 200
    body = response.json()
    assert body["risk_level"] == "WARNING"
    assert body["outputs"] == {"led_color": "YELLOW", "buzzer_mode": "BEEP"}
