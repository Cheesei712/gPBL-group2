from hmac import compare_digest
from typing import Annotated

from fastapi import Depends, Header, HTTPException, status

from app.config import Settings, get_settings


def verify_device_key(
    settings: Annotated[Settings, Depends(get_settings)],
    x_device_key: Annotated[str | None, Header()] = None,
) -> None:
    expected = settings.device_api_key.get_secret_value()
    if x_device_key is None or not compare_digest(x_device_key, expected):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid device credentials",
        )
