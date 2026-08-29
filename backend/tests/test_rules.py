import json
import random

import pytest

from app.gemini_service import LLM_ANALYSIS_SCHEMA, RuleBasedService
from app.models import BuzzerMode, Hazard, LedColor, RiskLevel, TelemetryRequest
from simulator import generate_telemetry


def test_gemini_wire_schema_avoids_unsupported_additional_properties() -> None:
    serialized = json.dumps(LLM_ANALYSIS_SCHEMA)
    assert "additionalProperties" not in serialized
    assert "additional_properties" not in serialized


@pytest.mark.parametrize(
    ("scenario", "risk", "hazard", "led", "buzzer"),
    [
        ("normal", RiskLevel.NORMAL, Hazard.NONE, LedColor.GREEN, BuzzerMode.OFF),
        ("rain", RiskLevel.WARNING, Hazard.HEAVY_RAIN, LedColor.YELLOW, BuzzerMode.BEEP),
        ("flood", RiskLevel.CRITICAL, Hazard.FLOOD, LedColor.RED, BuzzerMode.URGENT_BEEP),
        (
            "vibration",
            RiskLevel.WARNING,
            Hazard.ABNORMAL_VIBRATION,
            LedColor.YELLOW,
            BuzzerMode.BEEP,
        ),
        (
            "earthquake",
            RiskLevel.CRITICAL,
            Hazard.EARTHQUAKE,
            LedColor.RED,
            BuzzerMode.URGENT_BEEP,
        ),
        (
            "blizzard",
            RiskLevel.CRITICAL,
            Hazard.BLIZZARD,
            LedColor.RED,
            BuzzerMode.URGENT_BEEP,
        ),
        ("compound", RiskLevel.CRITICAL, Hazard.COMPOUND, LedColor.RED, BuzzerMode.URGENT_BEEP),
        ("sensor_error", RiskLevel.UNKNOWN, Hazard.SENSOR_ANOMALY, LedColor.BLUE, BuzzerMode.OFF),
    ],
)
@pytest.mark.asyncio
async def test_rule_scenarios(scenario, risk, hazard, led, buzzer) -> None:  # noqa: ANN001
    telemetry = TelemetryRequest.model_validate(generate_telemetry(scenario, random.Random(7)))
    result = await RuleBasedService().analyze(telemetry)
    assert (result.risk_level, result.hazard) == (risk, hazard)
    assert (result.outputs.led_color, result.outputs.buzzer_mode) == (led, buzzer)
