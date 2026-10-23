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
REM An existing local sdkconfig takes precedence over sdkconfig.defaults.
REM Catch missing large fonts before LVGL compilation fails mysteriously.
if exist "sdkconfig" (
  findstr /C:"CONFIG_LV_FONT_MONTSERRAT_40=y" "sdkconfig" >nul
  if errorlevel 1 (
    echo ERROR: Montserrat 40 font is disabled in your local sdkconfig.
    echo Run idf.py menuconfig and enable LVGL Montserrat 40 and 48 fonts.
    exit /b 1
  )
  findstr /C:"CONFIG_LV_FONT_MONTSERRAT_48=y" "sdkconfig" >nul
  if errorlevel 1 (
    echo ERROR: Montserrat 48 font is disabled in your local sdkconfig.
    echo Run idf.py menuconfig and enable LVGL Montserrat 40 and 48 fonts.
    exit /b 1
  )
)
echo Building GC8 gauge display...
call idf.py build
exit /b %errorlevel%
