from functools import lru_cache

from app.config import get_settings
from app.gemini_service import AnalysisService, GeminiService, RuleBasedService


@lru_cache
def get_analysis_service() -> AnalysisService:
    settings = get_settings()
    if settings.analysis_mode == "rules":
        return RuleBasedService()
    return GeminiService(settings)
