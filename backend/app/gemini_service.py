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
Bạn là bộ phân tích dữ liệu môi trường cho một nút IoT ESP32.
Chỉ sử dụng telemetry được cung cấp, không tự tạo thêm dữ liệu hoặc khẳng định chắc chắn
một thảm họa khi cảm biến không đủ khả năng chứng minh. Kết hợp nhiệt độ, độ ẩm,
mưa/hơi nước, mực nước, siêu âm và rung động. Nếu dữ liệu thiếu, mâu thuẫn hoặc cảm biến
không hợp lệ, chọn UNKNOWN hoặc SENSOR_ANOMALY. CRITICAL chỉ dành cho nguy cơ trực tiếp,
rõ ràng trong dữ liệu. Advice và reason phải bằng tiếng Việt, ngắn gọn, cụ thể.
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
            buzzer_mode=BuzzerMode.CONTINUOUS,
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
            "Hãy đánh giá telemetry IoT sau và trả về đúng schema JSON:\n"
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

        if not telemetry.all_sensors_valid:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.UNKNOWN,
                hazard=Hazard.SENSOR_ANOMALY,
                confidence_percent=100,
                advice="Kiểm tra kết nối và hiệu chuẩn các cảm biến báo lỗi.",
                reason="Có ít nhất một cảm biến không cung cấp dữ liệu hợp lệ.",
            )
        elif (water >= 85 or water_height >= 80) and vibration.peak_to_peak_raw >= 600:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.CRITICAL,
                hazard=Hazard.COMPOUND,
                confidence_percent=95,
                advice="Rời khu vực thấp và tránh công trình có rung động mạnh ngay lập tức.",
                reason="Mực nước rất cao đồng thời rung động vượt ngưỡng nguy hiểm.",
            )
        elif water >= 85 or water_height >= 80:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.CRITICAL,
                hazard=Hazard.FLOOD,
                confidence_percent=93,
                advice="Di chuyển lên vị trí cao và ngắt nguồn điện khu vực ngập.",
                reason="Mực nước đã vượt ngưỡng cảnh báo nghiêm trọng.",
            )
        elif vibration.peak_to_peak_raw >= 600 or vibration.rms_raw >= 100:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.WARNING,
                hazard=Hazard.ABNORMAL_VIBRATION,
                confidence_percent=88,
                advice="Tránh xa kết cấu không ổn định và theo dõi rung động.",
                reason="Biên độ rung đo được cao hơn mức vận hành thông thường.",
            )
        elif wet >= 70 and humidity >= 80:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.WARNING,
                hazard=Hazard.HEAVY_RAIN,
                confidence_percent=85,
                advice="Theo dõi mực nước và hạn chế đi qua khu vực trũng.",
                reason="Độ ẩm và độ ướt bề mặt cùng ở mức cao.",
            )
        else:
            analysis = LlmAnalysis(
                risk_level=RiskLevel.NORMAL,
                hazard=Hazard.NONE,
                confidence_percent=90,
                advice="Tiếp tục theo dõi cảm biến theo chu kỳ bình thường.",
                reason="Các chỉ số hiện chưa vượt ngưỡng cảnh báo.",
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
        advice="Không thể phân tích dữ liệu lúc này; tiếp tục theo dõi cảm biến.",
        reason="Dịch vụ LLM không khả dụng hoặc trả về dữ liệu không hợp lệ.",
        outputs=output_for_risk(RiskLevel.UNKNOWN),
    )
