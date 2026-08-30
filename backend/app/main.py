import logging
from typing import Annotated

from fastapi import Depends, FastAPI, HTTPException, status

from app.auth import verify_device_key
from app.config import get_settings
from app.dependencies import get_analysis_service
from app.gemini_service import AnalysisService, GeminiServiceError, unavailable_response
from app.models import AnalysisResponse, HealthResponse, TelemetryRequest

import httpx

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


async def sync_analysis_to_firebase(analysis: AnalysisResponse, device_id: str) -> None:
    firebase_url = settings.firebase_url.rstrip("/")
    if not firebase_url:
        return
    auth_secret = settings.firebase_auth.get_secret_value()
    auth_param = f"?auth={auth_secret}" if auth_secret else ""
    target_devices = {device_id, "esp32-node-01"}
    payload = analysis.model_dump(mode="json")

    try:
        async with httpx.AsyncClient(timeout=10.0) as client:
            for dev in target_devices:
                url = f"{firebase_url}/devices/{dev}/analysis.json{auth_param}"
                r = await client.put(url, json=payload)
                if r.status_code >= 300:
                    logger.warning("Firebase sync returned HTTP %d: %s", r.status_code, r.text)
                else:
                    logger.info("Successfully synced alert to Firebase: %s", url)
    except Exception as exc:
        logger.warning("Error syncing analysis to Firebase: %s", exc)


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
        result = await service.analyze(telemetry)
        await sync_analysis_to_firebase(result, telemetry.device_id)
        return result
    except (GeminiServiceError, ValueError, TypeError) as exc:
        logger.warning("Assessment failed for device %s: %s", telemetry.device_id, exc)
        fallback = unavailable_response(telemetry.device_id, settings.gemini_model)
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=fallback.model_dump(mode="json"),
        ) from exc

