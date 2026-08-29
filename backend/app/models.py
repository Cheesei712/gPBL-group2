from datetime import UTC, datetime
from enum import StrEnum

from pydantic import BaseModel, ConfigDict, Field


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class Dht11Reading(StrictModel):
    valid: bool
    temperature_c: float | None = Field(default=None, ge=-40, le=125)
    humidity_percent: float | None = Field(default=None, ge=0, le=100)


class Lm35Reading(StrictModel):
    valid: bool
    temperature_c: float | None = Field(default=None, ge=-55, le=150)


class UltrasonicReading(StrictModel):
    valid: bool
    echo_time_us: int = Field(ge=0)
    distance_cm: float | None = Field(default=None, ge=0)
    water_height_cm: float | None = Field(default=None, ge=0)


class SteamReading(StrictModel):
    valid: bool
    adc_raw: int = Field(ge=0, le=4095)
    wet_percent: float = Field(ge=0, le=100)


class WaterReading(StrictModel):
    valid: bool
    adc_raw: int = Field(ge=0, le=4095)
    level_percent: float = Field(ge=0, le=100)


class VibrationReading(StrictModel):
    valid: bool
    current_raw: int = Field(ge=0, le=4095)
    min_raw: int = Field(ge=0, le=4095)
    max_raw: int = Field(ge=0, le=4095)
    peak_to_peak_raw: int = Field(ge=0, le=4095)
    mean_raw: float = Field(ge=0, le=4095)
    rms_raw: float = Field(ge=0)
    event_count: int = Field(ge=0)
    saturated: bool


class TelemetryRequest(StrictModel):
    device_id: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_-]+$")
    timestamp_ms: int = Field(ge=0)
    dht11: Dht11Reading
    lm35: Lm35Reading
    hc_sr04: UltrasonicReading
    steam_sensor: SteamReading
    water_sensor: WaterReading
    ks0272_vibration: VibrationReading
    all_sensors_valid: bool


class RiskLevel(StrEnum):
    NORMAL = "NORMAL"
    WARNING = "WARNING"
    CRITICAL = "CRITICAL"
    UNKNOWN = "UNKNOWN"


class Hazard(StrEnum):
    NONE = "NONE"
    FLOOD = "FLOOD"
    HEAVY_RAIN = "HEAVY_RAIN"
    EXTREME_TEMPERATURE = "EXTREME_TEMPERATURE"
    ABNORMAL_VIBRATION = "ABNORMAL_VIBRATION"
    EARTHQUAKE = "EARTHQUAKE"
    BLIZZARD = "BLIZZARD"
    COMPOUND = "COMPOUND"
    SENSOR_ANOMALY = "SENSOR_ANOMALY"
    UNKNOWN = "UNKNOWN"


class LedColor(StrEnum):
    GREEN = "GREEN"
    YELLOW = "YELLOW"
    RED = "RED"
    BLUE = "BLUE"


class BuzzerMode(StrEnum):
    OFF = "OFF"
    BEEP = "BEEP"
    URGENT_BEEP = "URGENT_BEEP"


class LlmAnalysis(StrictModel):
    risk_level: RiskLevel
    hazard: Hazard
    confidence_percent: int = Field(ge=0, le=100)
    advice: str = Field(min_length=1, max_length=240)
    reason: str = Field(min_length=1, max_length=240)


class OutputCommand(StrictModel):
    led_color: LedColor
    buzzer_mode: BuzzerMode


class AnalysisResponse(StrictModel):
    valid: bool = True
    device_id: str
    analyzed_at: datetime = Field(default_factory=lambda: datetime.now(UTC))
    model: str
    risk_level: RiskLevel
    hazard: Hazard
    confidence_percent: int = Field(ge=0, le=100)
    advice: str = Field(min_length=1, max_length=240)
    reason: str = Field(min_length=1, max_length=240)
    outputs: OutputCommand


class HealthResponse(StrictModel):
    status: str = "ok"
    analysis_mode: str
