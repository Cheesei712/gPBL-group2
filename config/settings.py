"""Project configuration for thresholds, metadata, and alert settings."""

FIREBASE_SENSOR_PATH = "/sensors/node1"

SENSOR_META = {
    "temperature":  {"label": "Temperature",   "unit": "°C",   "icon": "🌡️",  "min": -10,  "max": 60},
    "humidity":     {"label": "Humidity",      "unit": "% RH", "icon": "💧",  "min": 0,    "max": 100},
    "distance":     {"label": "Distance",      "unit": "cm",   "icon": "📏",  "min": 0,    "max": 400},
    "steam_value":  {"label": "Steam Sensor",  "unit": "ADC",  "icon": "💦",  "min": 0,    "max": 4095},
    "water_level":  {"label": "Water Level",   "unit": "%",    "icon": "🌊",  "min": 0,    "max": 100},
    "accel_mag":    {"label": "Seismic Accel", "unit": "g",    "icon": "📳",  "min": 0,    "max": 5},
}

THRESHOLDS = {
    "water_level_warning":  60.0,
    "water_level_critical": 80.0,
    "distance_flood_warn":  100.0,
    "distance_flood_crit":  50.0,
    "steam_warning":        1500,
    "steam_critical":       3000,
    "temp_freeze":          2.0,
    "temp_blizzard":        -2.0,
    "humidity_flood_risk":  85.0,
    "accel_warning":        0.5,
    "accel_critical":       1.5,
}

ALERT_COLORS = {
    "NORMAL":   {"hex": "#22c55e", "bg": "rgba(34,197,94,0.15)",   "border": "#22c55e", "label": "Normal"},
    "WARNING":  {"hex": "#eab308", "bg": "rgba(234,179,8,0.15)",   "border": "#eab308", "label": "Warning"},
    "CRITICAL": {"hex": "#ef4444", "bg": "rgba(239,68,68,0.15)",   "border": "#ef4444", "label": "Critical"},
    "BLIZZARD": {"hex": "#3b82f6", "bg": "rgba(59,130,246,0.15)",  "border": "#3b82f6", "label": "Blizzard"},
}

HAZARD_ICONS = {
    "FLOOD":      "🌊",
    "EARTHQUAKE": "📳",
    "SNOWSTORM":  "❄️",
    "NORMAL":     "✅",
    "UNKNOWN":    "❓",
}

GEMINI_MODEL = "gemini-1.5-flash"
GEMINI_MAX_TOKENS = 300
GEMINI_TEMPERATURE = 0.1

EMAIL_MIN_INTERVAL_SECONDS = 300
EMAIL_SMTP_HOST = "smtp.gmail.com"
EMAIL_SMTP_PORT = 587

REFRESH_INTERVAL_SECONDS = 5
MAX_HISTORY_POINTS = 200
DEMO_FALLBACK = True
