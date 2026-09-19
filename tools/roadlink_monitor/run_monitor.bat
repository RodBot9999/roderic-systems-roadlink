@echo off
cd /d "%~dp0"
if exist RoadLinkMonitor.exe (
  RoadLinkMonitor.exe
  exit /b
)
where python >nul 2>nul
if errorlevel 1 (
  echo Python is not installed and RoadLinkMonitor.exe is missing.
  echo Use the standalone EXE supplied with the local project copy.
  pause
  exit /b 1
)
python roadlink_monitor.py
pause
