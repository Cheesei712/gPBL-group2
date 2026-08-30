"""
app.py — IoT Multi-Hazard Disaster Early Warning System
Streamlit Dashboard · Firebase Realtime DB + Backend AI + Gmail Alerts
"""

import os
import time
import sys
import json
import logging
from datetime import datetime, timezone, timedelta
from pathlib import Path

import streamlit as st

# Pandas is optional (may be blocked by Windows Application Control DLL policy)
try:
    import pandas as pd
    PANDAS_OK = True
except Exception:
    PANDAS_OK = False

import plotly.graph_objects as go

# ── Path setup ────────────────────────────────────────────────────────────────
ROOT = Path(__file__).parent
sys.path.insert(0, str(ROOT))

# ── Load .env ─────────────────────────────────────────────────────────────────
try:
    from dotenv import load_dotenv
    load_dotenv(ROOT / ".env")
except ImportError:
    pass

# ── Internal imports ──────────────────────────────────────────────────────────
from config.settings import (
    SENSOR_META, THRESHOLDS, ALERT_COLORS, HAZARD_ICONS,
    REFRESH_INTERVAL_SECONDS, FIREBASE_SENSOR_PATH,
)
import modules.firebase_client as fb
import modules.data_store as store
import modules.simulator as sim
from modules.email_notifier import send_alert_email, can_send_email

logging.basicConfig(level=logging.INFO)

# ─────────────────────────────────────────────────────────────────────────────
# Page config
# ─────────────────────────────────────────────────────────────────────────────

st.set_page_config(
    page_title="IoT Disaster EWS",
    page_icon="🌐",
    layout="wide",
    initial_sidebar_state="expanded",
)

# ─────────────────────────────────────────────────────────────────────────────
# CSS — Clean White / Light theme
# ─────────────────────────────────────────────────────────────────────────────

st.markdown("""
<style>
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap');

html, body, [data-testid="stAppViewContainer"] {
    background: #f8fafc !important;
    font-family: 'Inter', sans-serif !important;
    color: #1e293b !important;
}
[data-testid="stSidebar"] {
    background: #ffffff !important;
    border-right: 1px solid #e2e8f0 !important;
    box-shadow: 2px 0 12px rgba(0,0,0,0.06) !important;
}
[data-testid="stHeader"] {
    background: rgba(248,250,252,0.95) !important;
    border-bottom: 1px solid #e2e8f0 !important;
}
.block-container { padding: 4rem 2rem 2rem 2rem !important; max-width: 1600px !important; }

.section-header {
    font-size: 11px; font-weight: 700; letter-spacing: 2px;
    color: #94a3b8; text-transform: uppercase; margin-bottom: 12px;
    padding-bottom: 6px; border-bottom: 1px solid #e2e8f0;
}

/* ── Alert banners ─────────────────────────── */
.alert-banner {
    border-radius: 14px; padding: 20px 28px; margin-bottom: 20px;
    display: flex; align-items: center; gap: 20px;
    border: 1.5px solid; position: relative; overflow: hidden;
}
.alert-banner.NORMAL   { background: #f0fdf4; border-color: #86efac; }
.alert-banner.WARNING  { background: #fefce8; border-color: #fde047; }
.alert-banner.CRITICAL { background: #fff1f2; border-color: #fca5a5;
    animation: pulse-border 1.5s ease-in-out infinite; }
.alert-banner.BLIZZARD { background: #eff6ff; border-color: #93c5fd; }
@keyframes pulse-border {
    0%, 100% { box-shadow: 0 0 0 0 rgba(239,68,68,0.25); }
    50%       { box-shadow: 0 0 0 8px rgba(239,68,68,0.0); }
}

/* ── Sensor cards ────────────────────────────── */
.sensor-card {
    background: #ffffff; border: 1px solid #e2e8f0;
    border-radius: 16px; padding: 20px; text-align: center;
    transition: all 0.3s ease; position: relative; overflow: hidden;
    box-shadow: 0 1px 4px rgba(0,0,0,0.06);
}
.sensor-card:hover {
    border-color: #cbd5e1;
    transform: translateY(-3px); box-shadow: 0 10px 28px rgba(0,0,0,0.10);
}
.sensor-card .icon  { font-size: 28px; margin-bottom: 6px; }
.sensor-card .label { font-size: 11px; color: #94a3b8; font-weight: 600;
                       letter-spacing: 1px; text-transform: uppercase; }
.sensor-card .value { font-size: 28px; font-weight: 800; color: #1e293b; line-height: 1.1; margin: 4px 0; }
.sensor-card .unit  { font-size: 12px; color: #94a3b8; }
.sensor-card .bar-wrap { margin-top: 8px; background: #f1f5f9; border-radius: 4px; height: 5px; }
.sensor-card .bar-fill { height: 5px; border-radius: 4px; transition: width 0.5s ease; }

/* ── Panels ────────────────────────────── */
.glass-panel {
    background: #ffffff; border: 1px solid #e2e8f0;
    border-radius: 16px; padding: 20px;
    box-shadow: 0 1px 4px rgba(0,0,0,0.05);
}

/* ── AI badge ──────────────────────────── */
.ai-badge {
    display: inline-block; font-size: 10px; font-weight: 700;
    letter-spacing: 1px; padding: 3px 10px; border-radius: 100px;
    border: 1px solid currentColor; vertical-align: middle; margin-left: 8px;
}

/* ── LCD display ────────────────────────── */
.lcd-display {
    background: #1a1a2e; border: 2px solid #00d97e;
    border-radius: 8px; padding: 16px 20px; font-family: 'Courier New', monospace;
    font-size: 16px; color: #00d97e; letter-spacing: 0.5px;
    text-shadow: 0 0 8px rgba(0,217,126,0.6);
    min-height: 56px; display: flex; align-items: center;
}

/* ── LED indicator ──────────────────────── */
.led-dot {
    width: 18px; height: 18px; border-radius: 50%;
    display: inline-block; margin-right: 8px;
    box-shadow: 0 0 10px currentColor;
}
.led-GREEN  { background: #22c55e; color: #22c55e; }
.led-YELLOW { background: #eab308; color: #eab308; }
.led-RED    { background: #ef4444; color: #ef4444; animation: led-blink 0.8s ease-in-out infinite; }
.led-BLUE   { background: #3b82f6; color: #3b82f6; animation: led-blink 1.2s ease-in-out infinite; }
@keyframes led-blink { 0%,100%{opacity:1} 50%{opacity:0.25} }

/* ── Waiting / not connected ─────────────── */
.waiting-box {
    background: #f8fafc; border: 2px dashed #cbd5e1;
    border-radius: 16px; padding: 60px 40px; text-align: center;
}

/* ── Streamlit overrides ─────────────────── */
.stButton > button {
    background: linear-gradient(135deg, #3b82f6, #6366f1) !important;
    color: white !important; border: none !important; border-radius: 10px !important;
    font-weight: 600 !important; transition: all 0.2s !important;
}
.stButton > button:hover {
    transform: translateY(-1px) !important; box-shadow: 0 4px 18px rgba(99,102,241,0.35) !important;
}
div[data-testid="stMetric"] {
    background: #ffffff; border: 1px solid #e2e8f0;
    border-radius: 10px; padding: 12px;
    box-shadow: 0 1px 3px rgba(0,0,0,0.05);
}
div[data-testid="stMetricValue"] { color: #1e293b !important; }
div[data-testid="stMetricLabel"] { color: #64748b !important; }
.stSelectbox [data-baseweb="select"] { background: #f8fafc !important; border-color: #e2e8f0 !important; }
.stTextInput input { background: #f8fafc !important; border-color: #e2e8f0 !important; color: #1e293b !important; }
hr { border-color: #e2e8f0 !important; }
[data-testid="stTab"] button { color: #94a3b8 !important; font-weight: 500 !important; }
[data-testid="stTab"] button[aria-selected="true"] { color: #3b82f6 !important; border-color: #3b82f6 !important; font-weight: 700 !important; }
[data-testid="stSidebar"] * { color: #334155 !important; }
[data-testid="stSidebar"] .stMarkdown p { color: #64748b !important; }
.stToggle label { color: #475569 !important; }
[data-testid="stMarkdownContainer"] p { color: #475569 !important; }
::-webkit-scrollbar { width: 5px; }
::-webkit-scrollbar-track { background: #f1f5f9; }
::-webkit-scrollbar-thumb { background: #cbd5e1; border-radius: 3px; }
::-webkit-scrollbar-thumb:hover { background: #94a3b8; }
</style>
""", unsafe_allow_html=True)


# ─────────────────────────────────────────────────────────────────────────────
# Session state
# ─────────────────────────────────────────────────────────────────────────────

def _init_state():
    defaults = {
        "auto_refresh":       True,
        "refresh_interval":   REFRESH_INTERVAL_SECONDS,
        "gmail_sender":       os.getenv("GMAIL_SENDER", ""),
        "gmail_password":     os.getenv("GMAIL_APP_PASSWORD", ""),
        "gmail_recipient":    os.getenv("GMAIL_RECIPIENT", ""),
        "auto_email":         False,
        "email_on_warning":   True,
        "email_on_critical":  True,
        "last_assessment":    {},
        "last_sensor":        {},
        "email_status":       "",
        "fb_path":            os.getenv("FIREBASE_SENSOR_PATH", FIREBASE_SENSOR_PATH),
        # Simulator state
        "sim_active":         False,
        "sim_scenario":       "normal",
        "sim_intensity":      1.0,
        "sim_push_count":     0,
    }
    for k, v in defaults.items():
        if k not in st.session_state:
            st.session_state[k] = v

_init_state()


# ─────────────────────────────────────────────────────────────────────────────
# Sidebar
# ─────────────────────────────────────────────────────────────────────────────

with st.sidebar:
    st.markdown("""
    <div style='text-align:center;padding:16px 0 8px;'>
      <div style='font-size:36px;'>🌐</div>
      <div style='font-size:15px;font-weight:700;color:#1e293b;margin-top:4px;'>IoT Disaster EWS</div>
      <div style='font-size:11px;color:#94a3b8;letter-spacing:1px;'>EARLY WARNING SYSTEM</div>
    </div>
    <hr>
    """, unsafe_allow_html=True)

    # ── Firebase Connection ───────────────────────────────────────────────
    st.markdown('<div class="section-header">📡 Firebase Connection</div>', unsafe_allow_html=True)

    fb_url    = os.getenv("FIREBASE_DATABASE_URL", "")
    fb_path   = st.text_input("Sensor Data Path", value=st.session_state.fb_path,
                               key="fb_path_input", placeholder="/devices/esp32-node-01/telemetry",
                               help="Firebase Realtime DB path where ESP32 publishes data")
    if fb_path != st.session_state.fb_path:
        st.session_state.fb_path = fb_path

    if fb_url:
        st.markdown("""
        <div style='background:#f0fdf4;border:1px solid #86efac;border-radius:8px;
                    padding:8px 12px;font-size:12px;color:#166534;font-weight:600;'>
          ✅ Firebase URL configured
        </div>""", unsafe_allow_html=True)
    else:
        st.markdown("""
        <div style='background:#fff7ed;border:1px solid #fdba74;border-radius:8px;
                    padding:8px 12px;font-size:12px;color:#9a3412;'>
          ⚠️ Chưa có FIREBASE_DATABASE_URL trong .env
        </div>""", unsafe_allow_html=True)

    st.markdown("---")

    

    # ── Email Config ──────────────────────────────────────────────────────
    st.markdown('<div class="section-header">📧 Gmail Configuration</div>', unsafe_allow_html=True)

    sender_display = os.getenv("GMAIL_SENDER", "")
    if sender_display:
        st.markdown(f"""
        <div style='background:#f0fdf4;border:1px solid #86efac;border-radius:8px;
                    padding:8px 12px;font-size:12px;color:#166534;'>
          📨 Sending from:<br><strong>{sender_display}</strong>
        </div>""", unsafe_allow_html=True)
    else:
        st.markdown("""
        <div style='background:#fff7ed;border:1px solid #fdba74;border-radius:8px;
                    padding:8px 12px;font-size:12px;color:#9a3412;'>
          ⚠️ GMAIL_SENDER not set in .env
        </div>""", unsafe_allow_html=True)
    col_input, col_save = st.columns([3, 1])
    with col_input:
        recipient_val = st.text_input(
            "Recipients (comma-separated)", 
            value=st.session_state.gmail_recipient,
            placeholder="abc@gmail.com, xyz@yahoo.com",
            help="Enter multiple email addresses separated by commas to send bulk alerts."
        )
        st.session_state.gmail_recipient = recipient_val
        
    with col_save:
        st.markdown("<div style='margin-top:28px;'></div>", unsafe_allow_html=True)
        if st.button("💾 Save", use_container_width=True, help="Save to .env"):
            try:
                from dotenv import set_key
                set_key(str(ROOT / ".env"), "GMAIL_RECIPIENT", recipient_val)
                st.toast("✅ Recipients saved permanently!")
            except Exception as e:
                st.error(f"Save error: {e}")

    st.session_state.auto_email = st.toggle("Auto-send email on alerts", value=st.session_state.auto_email)
    if st.session_state.auto_email:
        col_w, col_c = st.columns(2)
        st.session_state.email_on_warning  = col_w.checkbox("Warning",  value=st.session_state.email_on_warning)
        st.session_state.email_on_critical = col_c.checkbox("Critical", value=st.session_state.email_on_critical)

    if st.button("📤 Send Email Now", use_container_width=True):
        if st.session_state.last_sensor and st.session_state.last_assessment:
            with st.spinner(f"Sending email..."):
                ok, msg = send_alert_email(st.session_state.last_assessment, st.session_state.last_sensor, st.session_state.gmail_recipient)
            st.session_state.email_status = f"✅ {msg}" if ok else f"❌ {msg}"
        else:
            st.session_state.email_status = "⚠️ No sensor data yet. Wait for the next update."

    if st.session_state.email_status:
        color = "#22c55e" if "✅" in st.session_state.email_status else "#ef4444"
        st.markdown(f'<div style="color:{color};font-size:12px;margin-top:6px;">{st.session_state.email_status}</div>',
                    unsafe_allow_html=True)

    st.markdown("---")

    # ── Refresh ───────────────────────────────────────────────────────────
    st.markdown('<div class="section-header">⚙️ Refresh Settings</div>', unsafe_allow_html=True)
    st.session_state.auto_refresh = st.toggle("Auto-refresh", value=st.session_state.auto_refresh)
    st.session_state.refresh_interval = st.slider("Interval (seconds)", 2, 60,
                                                    value=st.session_state.refresh_interval)
    if st.button("🔄 Refresh Now", use_container_width=True):
        st.rerun()

    if st.button("🗑️ Clear History", use_container_width=True):
        store.clear()
        st.success("History cleared!")
        st.rerun()

    csv_data = store.export_csv()
    if csv_data:
        st.download_button(
            "⬇️ Download CSV", data=csv_data,
            file_name=f"sensor_data_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
            mime="text/csv", use_container_width=True
        )

    st.markdown("---")
    st.markdown("""
    <div style='font-size:10px;color:#94a3b8;text-align:center;line-height:1.8;'>
      ESP32 · DHT11 · HC-SR04 · Steam · Water · MEMS<br>
      Firebase Realtime DB · Backend AI · Gmail SMTP<br>
      <span style='color:#e2e8f0;'>━━━━━━━━━━━━━━━━━━━━━━━</span><br>
      🌐 IoT Multi-Hazard EWS v2.0
    </div>
    """, unsafe_allow_html=True)


# ─────────────────────────────────────────────────────────────────────────────
# Data fetch cycle
# ─────────────────────────────────────────────────────────────────────────────

def _rule_based_assessment(d: dict) -> dict:
    from config.settings import THRESHOLDS
    temp      = d.get("temperature", 25)
    hum       = d.get("humidity", 50)
    dist      = d.get("distance", 400)
    steam     = d.get("steam_value", 0)
    water_lvl = d.get("water_level", 0)
    accel_mag = d.get("accel_mag", 0)

    alert   = "NORMAL"
    hazard  = "NORMAL"
    color   = "GREEN"
    buzzer  = False
    message = "System normal. No alerts."
    reason  = "All parameters within normal thresholds."
    conf    = 0.95

    if accel_mag >= THRESHOLDS["accel_critical"]:
        alert, hazard, color, buzzer = "CRITICAL", "EARTHQUAKE", "RED", True
        message = "STRONG EARTHQUAKE! Drop, cover, and hold on!"
        reason  = f"Seismic acceleration {accel_mag:.2f}g exceeded critical threshold."
        conf    = 0.98
    elif accel_mag >= THRESHOLDS["accel_warning"]:
        alert, hazard, color, buzzer = "WARNING", "EARTHQUAKE", "YELLOW", True
        message = "WARNING: Abnormal vibration. Monitor immediately."
        reason  = f"Acceleration {accel_mag:.2f}g indicates seismic tremor."
        conf    = 0.85
    elif water_lvl >= THRESHOLDS["water_level_critical"] or (
        dist <= THRESHOLDS["distance_flood_crit"] and hum >= THRESHOLDS["humidity_flood_risk"]
    ):
        alert, hazard, color, buzzer = "CRITICAL", "FLOOD", "RED", True
        message = "FLASH FLOOD CRITICAL! Evacuate to higher ground!"
        reason  = f"Water level {water_lvl:.0f}%, distance {dist}cm, humidity {hum:.0f}%."
        conf    = 0.92
    elif water_lvl >= THRESHOLDS["water_level_warning"] or steam >= THRESHOLDS["steam_critical"]:
        alert, hazard, color, buzzer = "WARNING", "FLOOD", "YELLOW", False
        message = "FLOOD WARNING: Water rising. Monitor closely."
        reason  = f"Water level {water_lvl:.0f}% or heavy rain ({steam} ADC)."
        conf    = 0.80
    elif temp <= THRESHOLDS["temp_blizzard"] and dist <= 100 and steam >= THRESHOLDS["steam_warning"]:
        alert, hazard, color, buzzer = "CRITICAL", "SNOWSTORM", "BLUE", True
        message = "BLIZZARD! Do not go outside. Stay safe indoors!"
        reason  = f"Temperature {temp}°C, snow accumulating rapidly, {dist}cm to ground."
        conf    = 0.88
    elif temp <= THRESHOLDS["temp_freeze"] and steam >= THRESHOLDS["steam_warning"]:
        alert, hazard, color, buzzer = "WARNING", "SNOWSTORM", "BLUE", False
        message = "FREEZE WARNING: Low temperature, snow falling."
        reason  = f"Temperature {temp}°C, moisture sensor: {steam} ADC."
        conf    = 0.78

    return {
        "alert_level": alert, "hazard_type": hazard, "rgb_color": color,
        "buzzer_active": buzzer, "lcd_message": message,
        "confidence": conf, "reasoning": reason, "source": "rule-based",
    }


def fetch_and_analyze():
    active_path = st.session_state.fb_path

    if st.session_state.sim_active:
        active_path = "/devices/simulator/telemetry"
        sim_data = sim.generate_sensor_data(st.session_state.sim_scenario, st.session_state.sim_intensity)
        fb.write_sensor_data(sim_data, active_path)
        st.session_state.sim_push_count += 1

    sensor = fb.get_sensor_data(active_path)
    if sensor is None:
        return None, None

    if sensor.get("alert_level"):
        assessment = {
            "alert_level":   sensor.get("alert_level",  "NORMAL"),
            "hazard_type":   sensor.get("hazard_type",  "NORMAL"),
            "rgb_color":     sensor.get("rgb_color",    "GREEN"),
            "buzzer_active": sensor.get("buzzer_active", False),
            "lcd_message":   sensor.get("lcd_message",  "System normal."),
            "confidence":    float(sensor.get("confidence", 1.0)),
            "reasoning":     sensor.get("reasoning",   ""),
            "source":        sensor.get("source",       "firebase"),
        }
    else:
        assessment = _rule_based_assessment(sensor)

    import modules.data_store as store
    store.push(sensor, assessment)
    st.session_state.last_sensor = sensor
    st.session_state.last_assessment = assessment

    if st.session_state.auto_email:
        lvl = assessment.get("alert_level", "NORMAL")
        should_send = (
            (lvl == "WARNING"  and st.session_state.email_on_warning)  or
            (lvl == "CRITICAL" and st.session_state.email_on_critical)
        ) and can_send_email()
        if should_send and lvl != "NORMAL":
            send_alert_email(assessment, sensor, st.session_state.gmail_recipient)

    return sensor, assessment



sensor_data, assessment = fetch_and_analyze()


# ─────────────────────────────────────────────────────────────────────────────
# Helper functions
# ─────────────────────────────────────────────────────────────────────────────

def _accent(rgb_color: str) -> str:
    return {"GREEN": "#22c55e", "YELLOW": "#eab308", "RED": "#ef4444", "BLUE": "#3b82f6"}.get(rgb_color, "#6b7280")

def _alert_css_class(alert_level: str, rgb_color: str) -> str:
    if rgb_color == "BLUE":
        return "BLIZZARD"
    return alert_level

def sensor_card_html(icon, label, value, unit, bar_pct: float = 0.0, accent_hex: str = "#6366f1") -> str:
    bar_pct = max(0.0, min(100.0, bar_pct))
    return f"""
    <div class="sensor-card">
      <div class="icon">{icon}</div>
      <div class="label">{label}</div>
      <div class="value">{value}</div>
      <div class="unit">{unit}</div>
      <div class="bar-wrap">
        <div class="bar-fill" style="width:{bar_pct:.1f}%;background:{accent_hex};"></div>
      </div>
    </div>
    """

level_map  = {"NORMAL": "Normal",  "WARNING": "Warning",    "CRITICAL": "CRITICAL"}
hazard_map = {"FLOOD":  "Flood",   "EARTHQUAKE": "Earthquake", "SNOWSTORM": "Snowstorm",
              "COMPOUND": "Compound", "NORMAL": "Normal",    "UNKNOWN": "Unknown"}


# ─────────────────────────────────────────────────────────────────────────────
# Waiting screen — Firebase not ready
# ─────────────────────────────────────────────────────────────────────────────

if sensor_data is None:
    st.markdown("""
    <div style='margin-bottom:4px;'>
      <span style='font-size:26px;font-weight:800;color:#1e293b;'>🌐 IoT Disaster EWS</span>
    </div>
    <div style='font-size:13px;color:#64748b;'>
      Multi-Hazard Early Warning System &nbsp;·&nbsp; ESP32 + Firebase + Backend AI
    </div>
    """, unsafe_allow_html=True)

    st.markdown("---")

    missing_items = []
    fb_url_val = os.getenv("FIREBASE_DATABASE_URL", "")
    if not fb_url_val:
        missing_items.append("Điền <code>FIREBASE_DATABASE_URL</code> vào file <b>.env</b>")
    else:
        missing_items.append("Chưa có dữ liệu tại path <b>" + st.session_state.fb_path + "</b> — kiểm tra ESP32 đang gửi dữ liệu")

    steps_html = "".join(f"<li style='margin:6px 0;'>{s}</li>" for s in missing_items)

    st.markdown(f"""
    <div class="waiting-box">
      <div style='font-size:56px;margin-bottom:16px;'>📡</div>
      <div style='font-size:20px;font-weight:700;color:#1e293b;margin-bottom:8px;'>
        Waiting for Firebase Data
      </div>
      <div style='font-size:14px;color:#64748b;max-width:520px;margin:0 auto 20px;line-height:1.7;'>
        The dashboard is ready. To start receiving live sensor data, complete the following:
      </div>
      <ul style='text-align:left;display:inline-block;color:#475569;font-size:13px;line-height:1.8;'>
        {steps_html}
      </ul>
    </div>
    """, unsafe_allow_html=True)

    st.markdown("---")

    with st.expander("📖 Setup Guide", expanded=True):
        st.markdown("""
**Step 1 — Edit .env**

Open `.env` (already created in project root) and fill in:
```
FIREBASE_DATABASE_URL=https://gpbl-group2-default-rtdb.asia-southeast1.firebasedatabase.app
FIREBASE_SENSOR_PATH=/devices/esp32-node-01/telemetry
GMAIL_SENDER=your@gmail.com
GMAIL_SENDER=your@gmail.com
GMAIL_APP_PASSWORD=xxxx xxxx xxxx xxxx
GMAIL_RECIPIENT=alert@gmail.com
```

**Step 3 — Restart**

After filling in `.env`, press **🔄 Refresh Now** in the sidebar or restart the app.
        """)

    if st.session_state.auto_refresh:
        time.sleep(st.session_state.refresh_interval)
        st.rerun()
    st.stop()


# ─────────────────────────────────────────────────────────────────────────────
# Main dashboard — Firebase data is live
# ─────────────────────────────────────────────────────────────────────────────

alert_lvl   = assessment.get("alert_level", "NORMAL")
hazard_type = assessment.get("hazard_type", "NORMAL")
rgb_color   = assessment.get("rgb_color", "GREEN")
lcd_msg     = assessment.get("lcd_message", "System normal.")
buzzer      = assessment.get("buzzer_active", False)
confidence  = assessment.get("confidence", 0.0)
reasoning   = assessment.get("reasoning", "")
ai_source   = assessment.get("source", "rule-based")

acc_cls = _alert_css_class(alert_lvl, rgb_color)
accent  = _accent(rgb_color)
h_icon  = HAZARD_ICONS.get(hazard_type, "❓")

# ── Header row ────────────────────────────────────────────────────────────────
col_title, col_status = st.columns([3, 1])
with col_title:
    src_lbl = sensor_data.get("source", "?").upper()
    st.markdown(f"""
    <div style='margin-bottom:4px;'>
      <span style='font-size:26px;font-weight:800;color:#1e293b;'>🌐 IoT Disaster EWS</span>
    </div>
    <div style='font-size:13px;color:#475569;margin-top:6px;'>
      Source: <strong style='color:#64748b;'>{src_lbl}</strong>
      &nbsp;·&nbsp; Path: <strong style='color:#64748b;'>{st.session_state.fb_path}</strong>
    </div>
    <div style='font-size:13px;color:#64748b;'>
      Multi-Hazard Early Warning System &nbsp;·&nbsp; ESP32 + Firebase + Backend AI
    </div>
    """, unsafe_allow_html=True)

with col_status:
    ts_raw = sensor_data.get("timestamp", "")
    try:
        ts_dt   = datetime.fromisoformat(ts_raw.replace("Z", "+00:00"))
        tz_vn   = timezone(timedelta(hours=7))
        ts_dt   = ts_dt.astimezone(tz_vn)
        ts_str  = ts_dt.strftime("%H:%M:%S")
        ts_date = ts_dt.strftime("%d/%m/%Y")
    except Exception:
        ts_str = "—"; ts_date = ""
    st.markdown(f"""
    <div style='text-align:right;'>
      <div style='font-size:11px;color:#94a3b8;'>Last data received</div>
      <div style='font-size:20px;font-weight:700;color:#1e293b;'>{ts_str}</div>
      <div style='font-size:10px;color:#94a3b8;'>{ts_date} VN Time</div>
    </div>
    """, unsafe_allow_html=True)

# ── Alert banner ──────────────────────────────────────────────────────────────
buzzer_html = f'<span style="font-size:16px;margin-left:16px;">{"🔔 Buzzer ACTIVE" if buzzer else "🔕 Buzzer off"}</span>'
src_badge   = f'<span class="ai-badge" style="color:{accent};">{"✨ BACKEND" if ai_source == "gemini" else "📐 RULE"}</span>'

st.markdown(f"""
<div class="alert-banner {acc_cls}">
  <div style='font-size:42px;line-height:1;'>{h_icon}</div>
  <div style='flex:1;'>
    <div style='font-size:11px;font-weight:700;letter-spacing:1.5px;color:{accent};text-transform:uppercase;'>
      {level_map.get(alert_lvl, alert_lvl)} · {hazard_map.get(hazard_type, hazard_type)} {src_badge}
    </div>
    <div style='font-size:17px;font-weight:600;color:#1e293b;margin-top:4px;line-height:1.3;'>
      {lcd_msg}
    </div>
    <div style='font-size:12px;color:#64748b;margin-top:6px;'>
      Confidence: <strong style='color:{accent};'>{confidence * 100:.0f}%</strong>
      {buzzer_html}
    </div>
  </div>
  <div>
    <span class="led-dot led-{rgb_color}"></span>
    <span style='font-size:12px;color:{accent};font-weight:700;'>{rgb_color}</span>
  </div>
</div>
""", unsafe_allow_html=True)


# ─────────────────────────────────────────────────────────────────────────────
# Tabs
# ─────────────────────────────────────────────────────────────────────────────

tab1, tab2, tab3, tab4 = st.tabs(["📊 Live Dashboard", "📋 Alert History", "🔬 Backend Analysis", "🧪 Simulation Control"])


# ══════════════════════════════════════════════════════════════════════════════
# TAB 1 — Live Dashboard
# ══════════════════════════════════════════════════════════════════════════════

with tab1:
    st.markdown('<div class="section-header">📡 Real-Time Sensor Readings</div>', unsafe_allow_html=True)

    temp      = sensor_data.get("temperature", 0.0)
    hum       = sensor_data.get("humidity", 0.0)
    dist      = sensor_data.get("distance", 400.0)
    steam_raw = sensor_data.get("steam_value", 0)
    water_lvl = sensor_data.get("water_level", 0.0)
    accel_mag = sensor_data.get("accel_mag", 0.0)

    steam_pct = steam_raw / 4095 * 100
    dist_pct  = max(0, 100 - (dist / 400 * 100))
    accel_pct = min(100, accel_mag / 5.0 * 100)

    def card_accent(key, val):
        if key == "water_level":
            if val >= THRESHOLDS["water_level_critical"]: return "#ef4444"
            if val >= THRESHOLDS["water_level_warning"]:  return "#eab308"
        elif key == "temperature":
            if val <= THRESHOLDS["temp_blizzard"]: return "#3b82f6"
            if val <= THRESHOLDS["temp_freeze"]:   return "#60a5fa"
        elif key == "steam":
            if val >= THRESHOLDS["steam_critical"]: return "#ef4444"
            if val >= THRESHOLDS["steam_warning"]:  return "#eab308"
        elif key == "accel":
            if val >= THRESHOLDS["accel_critical"]: return "#ef4444"
            if val >= THRESHOLDS["accel_warning"]:  return "#eab308"
        elif key == "humidity":
            if val >= THRESHOLDS["humidity_flood_risk"]: return "#eab308"
        return "#6366f1"

    c1, c2, c3, c4, c5, c6 = st.columns(6)
    cards = [
        (c1, sensor_card_html("🌡️", "Temperature", f"{temp:.1f}", "°C",
             (temp + 10) / 70 * 100, card_accent("temperature", temp))),
        (c2, sensor_card_html("💧", "Humidity", f"{hum:.1f}", "% RH",
             hum, card_accent("humidity", hum))),
        (c3, sensor_card_html("📏", "Distance", f"{dist:.0f}", "cm",
             dist_pct, "#6366f1")),
        (c4, sensor_card_html("💦", "Steam ADC", f"{steam_raw}", "/ 4095",
             steam_pct, card_accent("steam", steam_raw))),
        (c5, sensor_card_html("🌊", "Water Level", f"{water_lvl:.1f}", "%",
             water_lvl, card_accent("water_level", water_lvl))),
        (c6, sensor_card_html("📳", "Seismic", f"{accel_mag:.3f}", "g",
             accel_pct, card_accent("accel", accel_mag))),
    ]
    for col, html in cards:
        with col:
            st.markdown(html, unsafe_allow_html=True)

    st.markdown("---")

    # ── Time-series charts ────────────────────────────────────────────────
    st.markdown('<div class="section-header">📈 Historical Charts</div>', unsafe_allow_html=True)

    records = store.get_records()

    PLOTLY_LIGHT = dict(
        paper_bgcolor="#ffffff", plot_bgcolor="#f8fafc",
        font=dict(family="Inter", color="#64748b", size=11),
        margin=dict(l=8, r=8, t=32, b=8),
        xaxis=dict(gridcolor="#f1f5f9", zeroline=False, showgrid=True, linecolor="#e2e8f0"),
        yaxis=dict(gridcolor="#f1f5f9", zeroline=False, showgrid=True, linecolor="#e2e8f0"),
    )

    def make_chart(recs, col, color, title, yaxis_title, transform=None):
        xs = [r.get("timestamp") for r in recs]
        ys = [transform(r.get(col, 0)) if transform else r.get(col, 0) for r in recs]
        fig = go.Figure()
        if xs:
            fig.add_trace(go.Scatter(
                x=xs, y=ys, mode="lines", line=dict(color=color, width=2),
                fill="tozeroy", fillcolor=f"{color}18",
                hovertemplate=f"<b>{yaxis_title}:</b> %{{y:.2f}}<extra></extra>"
            ))
        fig.update_layout(**PLOTLY_LIGHT,
                          title=dict(text=title, font=dict(size=13, color="#475569")),
                          height=200, yaxis_title=yaxis_title)
        return fig

    row1_c1, row1_c2, row1_c3 = st.columns(3)
    with row1_c1:
        st.plotly_chart(make_chart(records, "temperature", "#fb923c", "🌡️ Temperature (°C)", "°C"),
                        use_container_width=True, config={"displayModeBar": False})
    with row1_c2:
        st.plotly_chart(make_chart(records, "humidity", "#38bdf8", "💧 Humidity (% RH)", "% RH"),
                        use_container_width=True, config={"displayModeBar": False})
    with row1_c3:
        st.plotly_chart(make_chart(records, "water_level", "#22d3ee", "🌊 Water Level (%)", "% Fill"),
                        use_container_width=True, config={"displayModeBar": False})

    row2_c1, row2_c2, row2_c3 = st.columns(3)
    with row2_c1:
        st.plotly_chart(make_chart(records, "distance", "#a78bfa", "📏 Distance (cm)", "cm"),
                        use_container_width=True, config={"displayModeBar": False})
    with row2_c2:
        st.plotly_chart(make_chart(records, "steam_value", "#34d399", "💦 Steam (%)", "% max",
                        transform=lambda v: v / 4095 * 100),
                        use_container_width=True, config={"displayModeBar": False})
    with row2_c3:
        st.plotly_chart(make_chart(records, "accel_mag", "#f87171", "📳 Seismic (g)", "g"),
                        use_container_width=True, config={"displayModeBar": False})

    # ── Hardware Status ───────────────────────────────────────────────────
    st.markdown("---")
    st.markdown('<div class="section-header">🖥️ Virtual Hardware Status (ESP32 HMI)</div>', unsafe_allow_html=True)

    hw1, hw2, hw3 = st.columns([2, 1, 1])
    with hw1:
        st.markdown('<div style="font-size:11px;color:#94a3b8;margin-bottom:6px;">📟 LCD Display</div>',
                    unsafe_allow_html=True)
        st.markdown(f'<div class="lcd-display">▶ {lcd_msg}</div>', unsafe_allow_html=True)
    with hw2:
        st.markdown('<div style="font-size:11px;color:#94a3b8;margin-bottom:6px;">🔆 RGB LED</div>',
                    unsafe_allow_html=True)
        st.markdown(f"""
        <div class="glass-panel" style="text-align:center;padding:16px;">
          <span class="led-dot led-{rgb_color}" style="width:32px;height:32px;display:inline-block;"></span>
          <div style="margin-top:10px;color:{accent};font-weight:700;font-size:14px;">{rgb_color}</div>
        </div>
        """, unsafe_allow_html=True)
    with hw3:
        st.markdown('<div style="font-size:11px;color:#94a3b8;margin-bottom:6px;">🔔 Buzzer</div>',
                    unsafe_allow_html=True)
        buzzer_color = "#ef4444" if buzzer else "#94a3b8"
        buzzer_label = "ACTIVE" if buzzer else "OFF"
        buzzer_icon  = "🔔" if buzzer else "🔕"
        st.markdown(f"""
        <div class="glass-panel" style="text-align:center;padding:16px;border-color:{buzzer_color}55;">
          <div style="font-size:28px;">{buzzer_icon}</div>
          <div style="margin-top:8px;color:{buzzer_color};font-weight:700;font-size:14px;">{buzzer_label}</div>
        </div>
        """, unsafe_allow_html=True)


# ══════════════════════════════════════════════════════════════════════════════
# TAB 2 — Alert History
# ══════════════════════════════════════════════════════════════════════════════

with tab2:
    st.markdown('<div class="section-header">📋 Alert History</div>', unsafe_allow_html=True)

    alert_records = store.get_alert_records()

    if not alert_records:
        st.markdown("""
        <div style="text-align:center;padding:60px 20px;color:#64748b;">
          <div style="font-size:48px;margin-bottom:12px;">✅</div>
          <div style="font-size:16px;font-weight:600;color:#475569;">No alerts yet</div>
          <div style="font-size:13px;color:#94a3b8;margin-top:6px;">System is operating normally</div>
        </div>
        """, unsafe_allow_html=True)
    else:
        total_alerts   = len(alert_records)
        critical_count = sum(1 for r in alert_records if r.get("alert_level") == "CRITICAL")
        warning_count  = sum(1 for r in alert_records if r.get("alert_level") == "WARNING")
        last_hazard    = alert_records[-1].get("hazard_type", "UNKNOWN")

        sc1, sc2, sc3, sc4 = st.columns(4)
        sc1.metric("Total Alerts", total_alerts)
        sc2.metric("🔴 Critical", critical_count)
        sc3.metric("🟡 Warning",  warning_count)
        sc4.metric("Last Hazard", HAZARD_ICONS.get(last_hazard, "?") + " " + hazard_map.get(last_hazard, last_hazard))

        from collections import Counter
        col_pie, col_tbl = st.columns([1, 2])
        with col_pie:
            hazard_counter = Counter(r.get("hazard_type", "UNKNOWN") for r in alert_records)
            hmap_colors    = {"FLOOD": "#3b82f6", "EARTHQUAKE": "#ef4444",
                              "SNOWSTORM": "#60a5fa", "COMPOUND": "#f59e0b"}
            labels = list(hazard_counter.keys())
            values = list(hazard_counter.values())
            fig_pie = go.Figure(go.Pie(
                labels=labels, values=values, hole=0.55,
                marker_colors=[hmap_colors.get(h, "#6b7280") for h in labels],
                textinfo="label+percent", textfont=dict(size=11, color="#1e293b"),
            ))
            fig_pie.update_layout(
                paper_bgcolor="#ffffff", plot_bgcolor="#ffffff",
                font=dict(family="Inter", color="#64748b", size=11),
                margin=dict(l=8, r=8, t=32, b=8),
                title=dict(text="Hazard Type Distribution", font=dict(size=13, color="#475569")),
                height=260, showlegend=False,
            )
            st.plotly_chart(fig_pie, use_container_width=True, config={"displayModeBar": False})

        with col_tbl:
            display_rows = []
            for r in reversed(alert_records[-50:]):
                ts = r.get("timestamp", "")
                try:
                    if isinstance(ts, str):
                        dt = datetime.fromisoformat(ts.replace("Z", "+00:00"))
                        tz_vn = timezone(timedelta(hours=7))
                        ts = dt.astimezone(tz_vn).strftime("%d/%m %H:%M:%S")
                    elif hasattr(ts, "strftime"):
                        ts = ts.strftime("%d/%m %H:%M:%S")
                except Exception:
                    pass
                display_rows.append({
                    "Time":       str(ts),
                    "Level":      r.get("alert_level", ""),
                    "Hazard":     r.get("hazard_type", ""),
                    "Message":    r.get("lcd_message", ""),
                    "Confidence": f"{r.get('confidence', 0) * 100:.0f}%",
                    "Source":     r.get("source", ""),
                })
            if PANDAS_OK:
                st.dataframe(pd.DataFrame(display_rows), use_container_width=True,
                             height=240, hide_index=True)
            else:
                st.table(display_rows)


# ══════════════════════════════════════════════════════════════════════════════
# TAB 3 — AI Analysis
# ══════════════════════════════════════════════════════════════════════════════

with tab3:
    st.markdown('<div class="section-header">🔬 Backend Analysis</div>', unsafe_allow_html=True)

    ai_c1, ai_c2 = st.columns([1, 1])

    with ai_c1:
        st.markdown(f"""
        <div class="glass-panel" style="border-color:{accent}55;">
          <div style="font-size:11px;color:{accent};font-weight:700;letter-spacing:1px;margin-bottom:12px;">
            CURRENT ASSESSMENT · {ai_source.upper()}
          </div>
          <table style="width:100%;border-collapse:collapse;">
            <tr><td style="color:#64748b;padding:6px 0;font-size:13px;width:45%;">Alert Level</td>
                <td style="color:{accent};font-weight:700;">{level_map.get(alert_lvl, alert_lvl)} ({alert_lvl})</td></tr>
            <tr><td style="color:#64748b;padding:6px 0;font-size:13px;">Hazard Type</td>
                <td style="color:#1e293b;font-weight:600;">{h_icon} {hazard_map.get(hazard_type, hazard_type)}</td></tr>
            <tr><td style="color:#64748b;padding:6px 0;font-size:13px;">RGB LED</td>
                <td style="color:{accent};font-weight:600;"><span class="led-dot led-{rgb_color}"></span>{rgb_color}</td></tr>
            <tr><td style="color:#64748b;padding:6px 0;font-size:13px;">Buzzer</td>
                <td style="color:{'#ef4444' if buzzer else '#22c55e'};font-weight:600;">{'🔔 Active' if buzzer else '🔕 Off'}</td></tr>
            <tr><td style="color:#64748b;padding:6px 0;font-size:13px;">Confidence</td>
                <td style="color:#1e293b;font-weight:700;">{confidence * 100:.1f}%</td></tr>
          </table>
          <div style="margin-top:14px;padding-top:14px;border-top:1px solid #e2e8f0;">
            <div style="font-size:11px;color:#94a3b8;margin-bottom:6px;">ANALYSIS REASONING</div>
            <div style="font-size:13px;color:#475569;line-height:1.6;">{reasoning or "—"}</div>
          </div>
        </div>
        """, unsafe_allow_html=True)

    with ai_c2:
        categories = ["Temperature", "Humidity", "Water Level", "Rain Sensor", "Seismic", "Distance"]
        temp_risk  = max(0, (1 - (temp  - THRESHOLDS["temp_blizzard"]) / 30)) * 100
        hum_risk   = max(0, (hum  - 50) / 50 * 100)
        water_risk = water_lvl
        steam_risk = steam_raw / 4095 * 100
        accel_risk = min(100, accel_mag / 5.0 * 100)
        dist_risk  = max(0, 100 - dist / 400 * 100)
        values        = [temp_risk, hum_risk, water_risk, steam_risk, accel_risk, dist_risk]
        values_closed = values + [values[0]]
        cats_closed   = categories + [categories[0]]

        fig_radar = go.Figure(go.Scatterpolar(
            r=values_closed, theta=cats_closed,
            fill="toself", fillcolor=f"{accent}22",
            line=dict(color=accent, width=2),
            marker=dict(size=6, color=accent),
        ))
        fig_radar.update_layout(
            paper_bgcolor="#ffffff",
            font=dict(family="Inter", color="#64748b", size=11),
            margin=dict(l=8, r=8, t=40, b=8),
            polar=dict(
                bgcolor="#f8fafc",
                radialaxis=dict(visible=True, range=[0, 100], gridcolor="#e2e8f0",
                                tickfont=dict(color="#94a3b8", size=9)),
                angularaxis=dict(gridcolor="#e2e8f0", tickfont=dict(color="#64748b", size=10)),
            ),
            title=dict(text="🕸️ Multi-Dimensional Risk Chart", font=dict(size=13, color="#475569")),
            height=320,
        )
        st.plotly_chart(fig_radar, use_container_width=True, config={"displayModeBar": False})

    with st.expander("📄 View Raw JSON Response", expanded=False):
        st.code(json.dumps(assessment, indent=2, ensure_ascii=False), language="json")

    with st.expander("📄 View Raw Sensor Data", expanded=False):
        st.code(json.dumps(sensor_data, indent=2, ensure_ascii=False, default=str), language="json")


# ══════════════════════════════════════════════════════════════════════════════
# TAB 4 — Simulation Control
# ══════════════════════════════════════════════════════════════════════════════

with tab4:
    st.markdown('<div class="section-header">🧪 Hardware Simulator</div>', unsafe_allow_html=True)
    st.markdown("Simulate hazards without physical sensors. Data is written to `/devices/simulator/telemetry` — separate from real hardware data.")

    if st.session_state.sim_active:
        st.info(f"🟢 Simulator **ACTIVE** — Scenario: `{st.session_state.sim_scenario.upper()}` | Pushes: {st.session_state.sim_push_count}")
    else:
        st.info("⏸️ Simulator is **OFF** — Dashboard is reading from real hardware path.")

    st.markdown("---")

    col1, col2 = st.columns([1, 2])
    with col1:
        scenarios = ["normal", "rain", "flood", "vibration", "earthquake", "blizzard", "compound"]
        current_idx = scenarios.index(st.session_state.sim_scenario) if st.session_state.sim_scenario in scenarios else 0
        st.session_state.sim_scenario = st.selectbox(
            "Hazard Scenario", scenarios,
            index=current_idx,
            format_func=lambda s: {
                "normal": "✅ Normal",
                "rain": "🌧️ Rain",
                "flood": "🌊 Flood",
                "vibration": "📳 Vibration",
                "earthquake": "📳 Earthquake",
                "blizzard": "❄️ Blizzard",
                "compound": "⚠️ Compound",
            }.get(s, s.title())
        )
        st.session_state.sim_intensity = st.slider(
            "Intensity", 0.0, 1.0, st.session_state.sim_intensity, 0.05,
            help="0.0 = minimal effect, 1.0 = maximum intensity"
        )

    with col2:
        st.markdown("**Quick launch:**")
        qc1, qc2, qc3, qc4 = st.columns(4)
        if qc1.button("🌊 Flood", use_container_width=True):
            st.session_state.sim_scenario = "flood"
            st.session_state.sim_intensity = 1.0
            st.session_state.sim_active = True
            st.rerun()
        if qc2.button("📳 Quake", use_container_width=True):
            st.session_state.sim_scenario = "earthquake"
            st.session_state.sim_intensity = 1.0
            st.session_state.sim_active = True
            st.rerun()
        if qc3.button("❄️ Blizzard", use_container_width=True):
            st.session_state.sim_scenario = "blizzard"
            st.session_state.sim_intensity = 1.0
            st.session_state.sim_active = True
            st.rerun()
        if qc4.button("🌧️ Rain", use_container_width=True):
            st.session_state.sim_scenario = "rain"
            st.session_state.sim_intensity = 0.7
            st.session_state.sim_active = True
            st.rerun()

    st.markdown("---")
    btn_c1, btn_c2 = st.columns(2)
    with btn_c1:
        if st.button("▶️ START SIMULATION", use_container_width=True, type="primary"):
            st.session_state.sim_active = True
            st.session_state.sim_push_count = 0
            st.rerun()
    with btn_c2:
        if st.button("⏹️ STOP & RESET", use_container_width=True):
            st.session_state.sim_active = False
            st.session_state.sim_push_count = 0
            st.session_state.sim_scenario = "normal"
            st.rerun()


# ─────────────────────────────────────────────────────────────────────────────
# Auto-refresh
# ─────────────────────────────────────────────────────────────────────────────

if st.session_state.auto_refresh:
    time.sleep(st.session_state.refresh_interval)
    st.rerun()
