import logging
from typing import Annotated

from fastapi import Depends, FastAPI, HTTPException, status

from app.auth import verify_device_key
from app.config import get_settings
from app.dependencies import get_analysis_service
from app.gemini_service import AnalysisService, GeminiServiceError, unavailable_response
from app.models import AnalysisResponse, HealthResponse, TelemetryRequest

settings = get_settings()
logging.basicConfig(
    level=getattr(logging, settings.log_level.upper(), logging.INFO),
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)
logger = logging.getLogger(__name__)

app = FastAPI(
    title="gPBL Disaster LLM Gateway",
    version="0.1.0",
    description="Validates ESP32 telemetry and obtains structured Gemini assessments.",
)


@app.get("/health", response_model=HealthResponse, tags=["operations"])
async def health() -> HealthResponse:
    return HealthResponse(analysis_mode=settings.analysis_mode)


@app.post(
    "/api/v1/analyze",
    response_model=AnalysisResponse,
    dependencies=[Depends(verify_device_key)],
    tags=["assessment"],
)
async def analyze(
    telemetry: TelemetryRequest,
    service: Annotated[AnalysisService, Depends(get_analysis_service)],
) -> AnalysisResponse:
    try:
        return await service.analyze(telemetry)
    except (GeminiServiceError, ValueError, TypeError) as exc:
        logger.warning("Assessment failed for device %s: %s", telemetry.device_id, exc)
        fallback = unavailable_response(telemetry.device_id, settings.gemini_model)
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=fallback.model_dump(mode="json"),
        ) from exc
