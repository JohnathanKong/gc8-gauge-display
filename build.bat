@echo off
setlocal
cd /d "%~dp0"
if not defined IDF_PATH (
  echo ERROR: ESP-IDF environment is not active.
  echo Run this from an ESP-IDF terminal in VS Code.
  exit /b 1
)
if not exist "sdkconfig" (
  echo Configuring ESP32-S3 target...
  call idf.py set-target esp32s3
  if errorlevel 1 exit /b 1
)
REM sdkconfig.defaults does not overwrite existing local sdkconfig.
REM Automatically migrate only the LVGL fonts required by the new screen.
echo Checking larger LVGL fonts in local sdkconfig...
python tools\ensure_lvgl_fonts.py
if errorlevel 1 exit /b 1
REM Reconfigure even on an incremental build so regenerated LVGL configuration
REM sees the updated sdkconfig before C++ compilation begins.
call idf.py reconfigure
if errorlevel 1 exit /b 1
echo Building GC8 gauge display...
call idf.py build
exit /b %errorlevel%
