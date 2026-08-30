import os
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

def can_send_email() -> bool:
    sender = os.getenv("GMAIL_SENDER", "")
    password = os.getenv("GMAIL_APP_PASSWORD", "")
    return bool(sender and password)

def send_alert_email(assessment: dict, sensor_data: dict, recipients_str: str = None) -> tuple[bool, str]:
    sender = os.getenv("GMAIL_SENDER", "")
    password = os.getenv("GMAIL_APP_PASSWORD", "")
    
    if not recipients_str:
        recipients_str = os.getenv("GMAIL_RECIPIENT", sender)
        
    if not sender or not password:
        return False, "Gmail credentials not configured in .env"
        
    # Parse multiple emails (comma-separated)
    to_addrs = [email.strip() for email in recipients_str.split(",") if email.strip()]
    
    if not to_addrs:
        return False, "No valid recipient emails found."

    level = assessment.get("alert_level", "NORMAL")
    hazard = assessment.get("hazard_type", "UNKNOWN")
    reason = assessment.get("reasoning", "")
    
    msg = MIMEMultipart()
    msg['From'] = sender
    msg['To'] = ", ".join(to_addrs)
    msg['Subject'] = f"[IoT Disaster EWS] {level} ALERT: {hazard}"
    
    body = f"""
Disaster Early Warning System Alert!

Alert Level: {level}
Hazard Type: {hazard}

Reason: {reason}

Sensor Data:
Temperature: {sensor_data.get('temperature', 'N/A')} C
Humidity: {sensor_data.get('humidity', 'N/A')} %
Water Level: {sensor_data.get('water_level', 'N/A')} %
Distance: {sensor_data.get('distance', 'N/A')} cm
Rain/Steam: {sensor_data.get('steam_value', 'N/A')} ADC
Seismic Accel: {sensor_data.get('accel_mag', 'N/A')} g
"""
    
    msg.attach(MIMEText(body, 'plain'))
    
    try:
        server = smtplib.SMTP('smtp.gmail.com', 587)
        server.starttls()
        server.login(sender, password)
        server.sendmail(sender, to_addrs, msg.as_string())
        server.quit()
        return True, f"Email sent successfully to {len(to_addrs)} recipient(s)"
    except Exception as e:
        return False, f"Failed to send email: {e}"
