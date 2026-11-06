@echo off
rem MiniCalc - збірка під Windows (MinGW gcc або Visual Studio cl)
chcp 65001 >nul
cd /d "%~dp0"
if not exist build mkdir build
where gcc >nul 2>nul
if %errorlevel%==0 (
  gcc -std=c99 -Os -Wall -municode -mwindows -static-libgcc -s -o build\MiniCalc.exe src\core.c src\plat_win32.c -lgdi32 -luser32
  goto done
)
where cl >nul 2>nul
if %errorlevel%==0 (
  rem /MT - статичний CRT: exe не потребує vcruntime*.dll
  cl /nologo /O1 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS src\core.c src\plat_win32.c /Fobuild\ /Fe:build\MiniCalc.exe user32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS
  goto done
)
echo Потрібен gcc (MinGW-w64) або cl (Visual Studio Build Tools).
exit /b 1
:done
if exist build\MiniCalc.exe echo Готово: build\MiniCalc.exe
