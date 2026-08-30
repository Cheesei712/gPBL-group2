import re

with open("D:/PJGBPL/app.py", "r", encoding="utf-8") as f:
    content = f.read()

# 1. Remove gemini import
content = re.sub(r'import modules\.gemini_client as gemini\n', '', content)

# 2. Remove gemini variables from _init_state
content = re.sub(r'\s*"use_gemini":.*?\n', '\n', content)
content = re.sub(r'\s*"gemini_api_key":.*?\n', '\n', content)
content = re.sub(r'\s*"gemini_call_count":.*?\n', '\n', content)

# 3. Remove Gemini AI sidebar section
sidebar_pattern = r'# ── Gemini AI ─────────────────────────────────────────────────────────.*?st\.markdown\("---"\)'
content = re.sub(sidebar_pattern, '', content, flags=re.DOTALL)

# 4. Replace fetch_and_analyze
new_fetch = '''def _rule_based_assessment(d: dict) -> dict:
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
            send_alert_email(assessment, sensor)

    return sensor, assessment
'''

fetch_pattern = r'def fetch_and_analyze\(\):.*?return sensor, assessment'
content = re.sub(fetch_pattern, new_fetch, content, flags=re.DOTALL)

# 5. Clean up Tab 3 Text & Tabs Header
content = content.replace('tab1, tab2, tab3, tab4 = st.tabs(["📊 Live Dashboard", "📋 Alert History", "🔬 AI Analysis", "🧪 Simulation Control"])',
                          'tab1, tab2, tab3, tab4 = st.tabs(["📊 Live Dashboard", "📋 Alert History", "🔬 Backend Analysis", "🧪 Simulation Control"])')

content = content.replace('<div class="section-header">🤖 Gemini AI Analysis</div>',
                          '<div class="section-header">🔬 Backend Analysis</div>')

content = content.replace("GEMINI_API_KEY=AIza...", "GMAIL_SENDER=your@gmail.com")
content = content.replace('✨ GEMINI', '✨ BACKEND')

# 6. Remove Gemini expanders in tab 3
expander_pattern = r'with st\.expander\("🔍 View Prompt Sent to Gemini", expanded=False\):.*?st\.code\(gemini\.SYSTEM_INSTRUCTION, language="text"\)'
content = re.sub(expander_pattern, '', content, flags=re.DOTALL)

with open("D:/PJGBPL/app.py", "w", encoding="utf-8") as f:
    f.write(content)
