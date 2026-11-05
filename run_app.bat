@echo off
rem Builds the C++ engine if needed, then starts the FoodLoop dashboard. Run from the project root.

cd /d "%~dp0"
if not exist bin\foodloop.exe call "%~dp0build.bat" || exit /b 1
python -m streamlit run app\streamlit_app.py
