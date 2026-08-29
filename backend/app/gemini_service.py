import logging
from typing import Protocol

from google import genai
from google.genai import types
from pydantic import ValidationError

from app.config import Settings
from app.models import (
    AnalysisResponse,
    BuzzerMode,
    Hazard,
    LedColor,
    LlmAnalysis,
    OutputCommand,
    RiskLevel,
    TelemetryRequest,
)

logger = logging.getLogger(__name__)

SYSTEM_INSTRUCTION = """
You analyze environmental telemetry for an ESP32 IoT sensor node.
Use only the supplied telemetry. Do not invent measurements or claim that a disaster is
certain when the sensors cannot prove it. Combine temperature, humidity, rain/steam,
water level, ultrasonic distance, and vibration. Select UNKNOWN or SENSOR_ANOMALY when
data is missing, inconsistent, or invalid. Reserve CRITICAL for a clear, immediate risk.

Detect EARTHQUAKE for very strong repeated vibration and BLIZZARD for temperatures below
0 C combined with high humidity. For the three demo scenarios: peak_to_peak_raw >= 1500
or rms_raw >= 300 means CRITICAL/EARTHQUAKE; water_height_cm >= 80 or level_percent >= 85
means CRITICAL/FLOOD; temperature <= 0 C and humidity_percent >= 70 means
WARNING/BLIZZARD. Prioritize these rules when the telemetry is valid.

Write advice and reason in concise English using ASCII characters so a 16x2 LCD can show
them. Advice must be actionable and no longer than 80 characters.
""".strip()

# Generate Content currently rejects Pydantic's `additionalProperties` keyword.
# Keep the wire schema intentionally small, then validate the response strictly with LlmAnalysis.
LLM_ANALYSIS_SCHEMA: dict[str, object] = {
    "type": "object",
    "properties": {
        "risk_level": {
            "type": "string",
            "enum": [risk.value for risk in RiskLevel],
        },
        "hazard": {
            "type": "string",
            "enum": [hazard.value for hazard in Hazard],
        },
        "confidence_percent": {
            "type": "integer",
            "minimum": 0,
            "maximum": 100,
        },
        "advice": {"type": "string"},
        "reason": {"type": "string"},
    },
    "required": [
        "risk_level",
        "hazard",
        "confidence_percent",
        "advice",
        "reason",
    ],
}


class GeminiServiceError(RuntimeError):
    """Raised when Gemini cannot return a validated analysis."""


class AnalysisService(Protocol):
    async def analyze(self, telemetry: TelemetryRequest) -> AnalysisResponse: ...


def output_for_risk(risk: RiskLevel) -> OutputCommand:
    mapping = {
        RiskLevel.NORMAL: OutputCommand(led_color=LedColor.GREEN, buzzer_mode=BuzzerMode.OFF),
        RiskLevel.WARNING: OutputCommand(
            led_color=LedColor.YELLOW,
            buzzer_mode=BuzzerMode.BEEP,
        ),
        RiskLevel.CRITICAL: OutputCommand(
            led_color=LedColor.RED,
            buzzer_mode=BuzzerMode.URGENT_BEEP,
        ),
        RiskLevel.UNKNOWN: OutputCommand(led_color=LedColor.BLUE, buzzer_mode=BuzzerMode.OFF),
    }
    return mapping[risk]


class GeminiService:
    def __init__(self, settings: Settings) -> None:
        self._model = settings.gemini_model
        api_key = settings.gemini_api_key.get_secret_value()
        if not api_key:
            raise GeminiServiceError("GEMINI_API_KEY is required in gemini mode")
        self._client = genai.Client(api_key=api_key)

    async def analyze(self, telemetry: TelemetryRequest) -> AnalysisResponse:
        prompt = (
            "Evaluate the following IoT telemetry and return the exact JSON schema:\n"
            f"{telemetry.model_dump_json(exclude_none=True)}"
        )
        try:
            response = await self._client.aio.models.generate_content(
                model=self._model,
                contents=prompt,
                config=types.GenerateContentConfig(
                    system_instruction=SYSTEM_INSTRUCTION,
                    temperature=0.1,
                    max_output_tokens=512,
                    response_mime_type="application/json",
                    response_schema=LLM_ANALYSIS_SCHEMA,
                ),
            )
            if response.parsed is not None:
                analysis = LlmAnalysis.model_validate(response.parsed)
            elif response.text:
                analysis = LlmAnalysis.model_validate_json(response.text)
            else:
                raise GeminiServiceError("Gemini returned an empty response")
        except (ValidationError, ValueError, TypeError, GeminiServiceError):
            logger.exception("Gemini returned an invalid structured response")
            raise
        except Exception as exc:
            logger.exception("Gemini request failed")
            raise GeminiServiceError("Gemini request failed") from exc

        # Hardware commands are deterministic and are never authored directly by the LLM.
        return AnalysisResponse(
            device_id=telemetry.device_id,
            model=self._model,
            risk_level=analysis.risk_level,
            hazard=analysis.hazard,
            confidence_percent=analysis.confidence_percent,
            advice=analysis.advice,
            reason=analysis.reason,
            outputs=output_for_risk(analysis.risk_level),
        )


class RuleBasedService:
    """Deterministic local analyzer for HTTP, simulator, and hardware testing."""

    async def analyze(self, telemetry: TelemetryRequest) -> AnalysisResponse:
        vibration = telemetry.ks0272_vibration
        water = telemetry.water_sensor.level_percent
        water_height = telemetry.hc_sr04.water_height_cm or 0
        wet = telemetry.steam_sensor.wet_percent
        humidity = telemetry.dht11.humidity_percent or 0
        temperatures = [
            value
            for value in (telemetry.dht11.temperature_c, telemetry.lm35.temperature_c)
            if value is not None
        ]
        minimum_temperature = min(temperatures, default=25.0)

        if not telemetry.all_sensors_valid:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.UNKNOWN,
                hazard=Hazard.SENSOR_ANOMALY,
                confidence_percent=100,
                advice="Check the wiring and calibration of every failed sensor.",
                reason="At least one sensor did not provide valid data.",
            )
        elif minimum_temperature <= 0 and humidity >= 70:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.CRITICAL,
                hazard=Hazard.BLIZZARD,
                confidence_percent=94,
                advice="Stay indoors, keep warm, and avoid outdoor travel.",
                reason="Subzero temperature and high humidity indicate blizzard risk.",
            )
        elif vibration.peak_to_peak_raw >= 1500 or vibration.rms_raw >= 300:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.CRITICAL,
                hazard=Hazard.EARTHQUAKE,
                confidence_percent=95,
                advice="Move away from glass and falling objects; take cover now.",
                reason="Vibration amplitude or energy exceeds the earthquake threshold.",
            )
        elif (water >= 85 or water_height >= 80) and vibration.peak_to_peak_raw >= 600:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.CRITICAL,
                hazard=Hazard.COMPOUND,
                confidence_percent=95,
                advice="Leave low ground and unstable structures immediately.",
                reason="Water is very high while vibration exceeds the danger threshold.",
            )
        elif water >= 85 or water_height >= 80:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.CRITICAL,
                hazard=Hazard.FLOOD,
                confidence_percent=93,
                advice="Move to higher ground and disconnect power in flooded areas.",
                reason="The water level exceeds the critical flood threshold.",
            )
        elif vibration.peak_to_peak_raw >= 600 or vibration.rms_raw >= 100:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.WARNING,
                hazard=Hazard.ABNORMAL_VIBRATION,
                confidence_percent=88,
                advice="Avoid unstable structures and monitor vibration.",
                reason="Measured vibration is above the normal operating range.",
            )
        elif wet >= 70 and humidity >= 80:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.WARNING,
                hazard=Hazard.HEAVY_RAIN,
                confidence_percent=85,
                advice="Monitor water levels and avoid low-lying areas.",
                reason="Humidity and surface wetness are both high.",
            )
        else:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.NORMAL,
                hazard=Hazard.NONE,
                confidence_percent=90,
                advice="Continue normal periodic sensor monitoring.",
                reason="No measurement currently exceeds an alert threshold.",
            )

        return AnalysisResponse(
            device_id=telemetry.device_id,
            model="local-rules-v1",
            risk_level=analysis.risk_level,
            hazard=analysis.hazard,
            confidence_percent=analysis.confidence_percent,
            advice=analysis.advice,
            reason=analysis.reason,
            outputs=output_for_risk(analysis.risk_level),
        )


def unavailable_response(device_id: str, model: str) -> AnalysisResponse:
    return AnalysisResponse(
        valid=False,
        device_id=device_id,
        model=model,
        risk_level=RiskLevel.UNKNOWN,
        hazard=Hazard.UNKNOWN,
        confidence_percent=0,
        advice="Analysis is unavailable; continue monitoring the sensors.",
        reason="The LLM service is unavailable or returned invalid data.",
        outputs=output_for_risk(RiskLevel.UNKNOWN),
    )
