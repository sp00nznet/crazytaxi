@echo off
rem Crazy Taxi quick start: checks what's needed, asks before installing
rem anything, builds from your own disc, and leaves "Play Crazy Taxi.cmd".
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1" %*
set rc=%errorlevel%
echo.
pause
exit /b %rc%
