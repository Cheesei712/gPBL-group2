@echo off
chcp 65001 >nul
title IoT Disaster EWS - Dashboard

echo.
echo  ============================================
echo   IoT Multi-Hazard Disaster Early Warning System
echo  ============================================
echo.

:: Check if .venv exists, if not create one
if not exist ".venv" (
    echo [*] Tao virtual environment...
    python -m venv .venv
)

:: Activate virtual environment
call .venv\Scripts\activate.bat

:: Check if streamlit is installed
python -c "import streamlit" 2>nul
if %errorlevel% neq 0 (
    echo [*] Cai dat thu vien tu requirements.txt...
    pip install -r requirements.txt
    echo.
)


echo [*] Khoi dong Dashboard tai http://localhost:8501
echo     Nhan Ctrl+C de dung
echo.

streamlit run app.py --server.port 8501 --server.headless false

pause
