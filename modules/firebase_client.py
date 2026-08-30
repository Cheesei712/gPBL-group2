"""
modules/firebase_client.py
Reads/writes Firebase Realtime Database via REST API (no service account needed).
Database rules must allow public read (currently set to true for /devices/**).
"""

import math
import json
import logging
import urllib.request
import urllib.error
from datetime import datetime, timezone
from typing import Optional, Dict, Any
import os

logger = logging.getLogger(__name__)

# ── Build the base URL once ────────────────────────────────────────────────────

def _db_url() -> str:
    url = os.getenv("FIREBASE_DATABASE_URL", "").rstrip("/")
    if not url:
        raise ValueError("FIREBASE_DATABASE_URL is not set in .env")
    return url


# ── REST helpers ───────────────────────────────────────────────────────────────

def _get_auth_param() -> str:
    """Return ?auth=SECRET if configured, else empty string."""
    secret = os.getenv("FIREBASE_AUTH_SECRET", "").strip()
    return f"?auth={secret}" if secret else ""

def _rest_get(path: str) -> Optional[Any]:
    """HTTP GET to Firebase REST endpoint. Returns parsed JSON or None."""
    try:
        url = f"{_db_url()}{path}.json{_get_auth_param()}"
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=5) as resp:
            raw = resp.read().decode("utf-8")
            return json.loads(raw)
    except urllib.error.HTTPError as e:
        logger.error(f"Firebase GET HTTP error {e.code}: {path}")
        return None
    except urllib.error.URLError as e:
        logger.error(f"Firebase GET connection error: {e.reason}")
        return None
    except Exception as e:
        logger.error(f"Firebase GET error: {e}")
        return None


def _rest_put(path: str, data: Dict[str, Any]) -> bool:
    """HTTP PUT to Firebase REST endpoint to overwrite a node."""
    try:
        url = f"{_db_url()}{path}.json{_get_auth_param()}"
        body = json.dumps(data, ensure_ascii=False, default=str).encode("utf-8")
        req = urllib.request.Request(
            url, data=body, method="PUT",
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status == 200
    except Exception as e:
        logger.error(f"Firebase PUT error: {e}")
        return False


# ── Public API ─────────────────────────────────────────────────────────────────

def get_sensor_data(path: str = "/sensors/node1") -> Optional[Dict[str, Any]]:
    """
    Fetch latest sensor snapshot from Firebase Realtime DB via REST.
    Returns normalized dict or None if unavailable.
    """
    data = _rest_get(path)
    if data and isinstance(data, dict):
        return _normalize(data)
    logger.warning(f"No data at Firebase path: {path}")
    return None


def write_sensor_data(data: Dict[str, Any], path: str = "/devices/simulator/telemetry") -> bool:
    """Write sensor data dict to Firebase path via REST. Used by simulator."""
    return _rest_put(path, data)


def is_configured() -> bool:
    """Return True if FIREBASE_DATABASE_URL is set in environment."""
    return bool(os.getenv("FIREBASE_DATABASE_URL", ""))


# ── Normalization ──────────────────────────────────────────────────────────────

def _normalize(raw: Dict[str, Any]) -> Dict[str, Any]:
    """Normalize Firebase data: map nested gPBL-group2 schema to flat dashboard schema."""
    
    # Extract from nested schema (if exists)
    dht11 = raw.get("dht11", {})
    hc_sr04 = raw.get("hc_sr04", {})
    steam = raw.get("steam_sensor", {})
    water = raw.get("water_sensor", {})
    vib = raw.get("ks0272_vibration", {})

    # Fallback to flat schema (for simulator or older data)
    temp      = float(dht11.get("temperature_c", raw.get("temperature", 0.0)))
    hum       = float(dht11.get("humidity_percent", raw.get("humidity", 0.0)))
    dist      = float(hc_sr04.get("distance_cm", raw.get("distance", 400.0)))
    steam_val = int(steam.get("adc_raw", raw.get("steam_value", 0)))
    water_lvl = float(water.get("level_percent", raw.get("water_level", 0.0)))

    # Accel mapping: use ks0272 peak_to_peak if nested, otherwise calculate from x,y,z
    ax = float(raw.get("accel_x", 0.0))
    ay = float(raw.get("accel_y", 0.0))
    az = float(raw.get("accel_z", 1.0))
    
    if "ks0272_vibration" in raw:
        # Convert peak_to_peak to a simulated g-force (approximate max 4095)
        # We divide by ~400 to map to 0..10g range
        accel_mag = float(vib.get("peak_to_peak_raw", 0)) / 400.0
    elif "accel_mag" in raw:
        accel_mag = float(raw["accel_mag"])
    else:
        accel_mag = abs(round(math.sqrt(ax**2 + ay**2 + az**2) - 1.0, 3))
        
    ts = raw.get("timestamp_ms")
    # If ts is a valid real-world epoch in milliseconds (e.g., > year 2020), parse it.
    # Otherwise (like ESP32 millis() which is relative uptime), fallback to current time.
    if isinstance(ts, (int, float)) and ts > 1600000000000:
        dt_str = datetime.fromtimestamp(ts / 1000.0, tz=timezone.utc).isoformat()
    else:
        # Fallback to current UTC time at the moment of fetching
        dt_str = raw.get("timestamp", datetime.now(timezone.utc).isoformat())

    return {
        "temperature":  temp,
        "humidity":     hum,
        "distance":     dist,
        "steam_value":  steam_val,
        "water_level":  water_lvl,
        "accel_x":      ax,
        "accel_y":      ay,
        "accel_z":      az,
        "accel_mag":    accel_mag,
        "timestamp":    dt_str,
        "source":       raw.get("source", "firebase"),
        
        # If ESP32 ever pushes analysis back to Firebase, grab it
        "alert_level":  raw.get("alert_level"),
        "hazard_type":  raw.get("hazard_type"),
        "rgb_color":    raw.get("rgb_color"),
        "buzzer_active":raw.get("buzzer_active"),
        "lcd_message":  raw.get("lcd_message"),
        "confidence":   raw.get("confidence"),
        "reasoning":    raw.get("reasoning")
    }
