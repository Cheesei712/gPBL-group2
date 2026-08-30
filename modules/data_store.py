"""In-memory store for sensor history and alert events."""

from collections import deque
from datetime import datetime, timezone
from typing import Any, Dict, List, Optional

from config.settings import MAX_HISTORY_POINTS

try:
    import pandas as pd
    PANDAS_OK = True
except Exception:
    PANDAS_OK = False

_history: deque = deque(maxlen=MAX_HISTORY_POINTS)
_alert_log: deque = deque(maxlen=200)


def push(sensor_data: Dict[str, Any], assessment: Dict[str, Any]) -> None:
    """Append a timestamped sensor + assessment snapshot to history."""
    ts_raw = sensor_data.get("timestamp", "")
    try:
        ts = datetime.fromisoformat(ts_raw.replace("Z", "+00:00"))
    except Exception:
        ts = datetime.now(timezone.utc)

    record = {
        "timestamp":   ts,
        "temperature": sensor_data.get("temperature", 0.0),
        "humidity":    sensor_data.get("humidity", 0.0),
        "distance":    sensor_data.get("distance", 400.0),
        "steam_value": sensor_data.get("steam_value", 0),
        "water_level": sensor_data.get("water_level", 0.0),
        "accel_mag":   sensor_data.get("accel_mag", 0.0),
        "alert_level": assessment.get("alert_level", "NORMAL"),
        "hazard_type": assessment.get("hazard_type", "NORMAL"),
        "rgb_color":   assessment.get("rgb_color", "GREEN"),
    }
    _history.append(record)

    if assessment.get("alert_level", "NORMAL") != "NORMAL":
        _alert_log.append({
            "timestamp":   ts,
            "alert_level": assessment.get("alert_level"),
            "hazard_type": assessment.get("hazard_type"),
            "lcd_message": assessment.get("lcd_message", ""),
            "confidence":  assessment.get("confidence", 0.0),
            "source":      assessment.get("source", ""),
        })


def get_records() -> List[Dict[str, Any]]:
    """Return history as a plain list of dicts."""
    return list(_history)


def get_alert_records() -> List[Dict[str, Any]]:
    """Return alert log as a plain list of dicts."""
    return list(_alert_log)


def get_dataframe():
    """Return history as pandas DataFrame if available, else plain list."""
    records = get_records()
    if PANDAS_OK:
        import pandas as pd  # noqa: F811
        if not records:
            return pd.DataFrame(columns=[
                "timestamp", "temperature", "humidity", "distance",
                "steam_value", "water_level", "accel_mag", "alert_level", "hazard_type"
            ])
        return pd.DataFrame(records)
    return records


def get_alert_log():
    """Return alert log as pandas DataFrame if available, else plain list."""
    records = get_alert_records()
    if PANDAS_OK:
        import pandas as pd  # noqa: F811
        if not records:
            return pd.DataFrame(columns=[
                "timestamp", "alert_level", "hazard_type", "lcd_message", "confidence", "source"
            ])
        return pd.DataFrame(records)
    return records


def get_latest() -> Optional[Dict[str, Any]]:
    """Return the most recent record or None."""
    return dict(_history[-1]) if _history else None


def clear() -> None:
    _history.clear()
    _alert_log.clear()


def export_csv() -> str:
    """Return CSV string of full history for download."""
    records = get_records()
    if not records:
        return ""
    if PANDAS_OK:
        return get_dataframe().to_csv(index=False)
    headers = list(records[0].keys())
    lines = [",".join(str(h) for h in headers)]
    for r in records:
        lines.append(",".join(str(r.get(h, "")) for h in headers))
    return "\n".join(lines)
