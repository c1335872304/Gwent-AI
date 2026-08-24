@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ============================================================
REM Gwent - Windows Local Card Test
REM Conda base Python: C:\ProgramData\miniconda3\python.exe
REM ============================================================

title Gwent Local Test Launcher

REM Project root = two levels above tools\windows\
cd /d "%~dp0\..\.."
set "PROJECT_ROOT=%CD%"

REM Use Conda base Python directly
set "PYTHON_EXE=C:\ProgramData\miniconda3\python.exe"

echo.
echo ============================================================
echo  GWENT LOCAL CARD TEST
echo ============================================================
echo Project : %PROJECT_ROOT%
echo Python  : %PYTHON_EXE%
echo.

REM ------------------------------------------------------------
REM 1. Check Python
REM ------------------------------------------------------------
if not exist "%PYTHON_EXE%" (
    echo [ERROR] Conda base Python not found:
    echo         %PYTHON_EXE%
    pause
    exit /b 1
)

"%PYTHON_EXE%" --version
echo.

REM ------------------------------------------------------------
REM 2. Locate Core DLL
REM ------------------------------------------------------------
set "GWENT_DLL="

if exist "%PROJECT_ROOT%\build-web\Release\gwent_core.dll" (
    set "GWENT_DLL=%PROJECT_ROOT%\build-web\Release\gwent_core.dll"
)

if not defined GWENT_DLL if exist "%PROJECT_ROOT%\build-vs\Release\gwent_core.dll" (
    set "GWENT_DLL=%PROJECT_ROOT%\build-vs\Release\gwent_core.dll"
)

if not defined GWENT_DLL (
    echo [ERROR] gwent_core.dll not found.
    echo.
    echo Checked:
    echo   %PROJECT_ROOT%\build-web\Release\gwent_core.dll
    echo   %PROJECT_ROOT%\build-vs\Release\gwent_core.dll
    echo.
    pause
    exit /b 1
)

echo [OK] Core DLL:
echo      %GWENT_DLL%
echo.

REM ------------------------------------------------------------
REM 3. Check npm
REM ------------------------------------------------------------
where npm.cmd >nul 2>&1
if errorlevel 1 (
    echo [ERROR] npm.cmd not found in PATH.
    pause
    exit /b 1
)

if not exist "%PROJECT_ROOT%\apps\web\frontend\node_modules" (
    echo [INFO] Frontend dependencies missing. Running npm install...
    pushd "%PROJECT_ROOT%\apps\web\frontend"
    call npm.cmd install
    if errorlevel 1 (
        popd
        echo [ERROR] npm install failed.
        pause
        exit /b 1
    )
    popd
)

REM ------------------------------------------------------------
REM 4. Start Core HTTP :8008
REM ------------------------------------------------------------
echo [START] Core HTTP :8008

start "Gwent Core 8008" /D "%PROJECT_ROOT%" cmd.exe /k ^
"set ""GWENT_CORE_LIBRARY=%GWENT_DLL%"" ^&^& ""%PYTHON_EXE%"" tools\server\human_vs_ai.py"

REM Wait for Core
echo [WAIT] Waiting for Core :8008 ...

set "CORE_READY=0"
for /L %%I in (1,1,20) do (
    powershell -NoProfile -Command ^
      "try { Invoke-WebRequest -UseBasicParsing -TimeoutSec 1 http://127.0.0.1:8008/health ^| Out-Null; exit 0 } catch { exit 1 }" >nul 2>&1

    if not errorlevel 1 (
        set "CORE_READY=1"
        goto :core_ready
    )

    timeout /t 1 /nobreak >nul
)

:core_ready
if "%CORE_READY%"=="0" (
    echo.
    echo [ERROR] Core did not start on port 8008.
    echo Check the window titled "Gwent Core 8008".
    echo.
    pause
    exit /b 1
)

echo [OK] Core :8008 is responding.

REM ------------------------------------------------------------
REM 5. Start FastAPI :8010
REM IMPORTANT: run as module, not "python app\main.py"
REM ------------------------------------------------------------
echo [START] FastAPI :8010

start "Gwent Backend 8010" /D "%PROJECT_ROOT%\apps\web\backend" cmd.exe /k ^
"""%PYTHON_EXE%"" -m uvicorn app.main:app --host 127.0.0.1 --port 8010"

REM Wait for backend
echo [WAIT] Waiting for FastAPI :8010 ...

set "BACKEND_READY=0"
for /L %%I in (1,1,20) do (
    powershell -NoProfile -Command ^
      "try { Invoke-WebRequest -UseBasicParsing -TimeoutSec 1 http://127.0.0.1:8010/api/health ^| Out-Null; exit 0 } catch { exit 1 }" >nul 2>&1

    if not errorlevel 1 (
        set "BACKEND_READY=1"
        goto :backend_ready
    )

    timeout /t 1 /nobreak >nul
)

:backend_ready
if "%BACKEND_READY%"=="0" (
    echo.
    echo [ERROR] FastAPI did not start on port 8010.
    echo Check the window titled "Gwent Backend 8010".
    echo.
    pause
    exit /b 1
)

echo [OK] FastAPI :8010 is responding.

REM ------------------------------------------------------------
REM 6. Start React :5173
REM ------------------------------------------------------------
echo [START] React :5173

start "Gwent Frontend 5173" /D "%PROJECT_ROOT%\apps\web\frontend" cmd.exe /k ^
"npm.cmd run dev"

timeout /t 2 /nobreak >nul

REM ------------------------------------------------------------
REM 7. Open browser
REM ------------------------------------------------------------
echo.
echo ============================================================
echo  ALL SERVICES READY
echo ============================================================
echo Core     : http://127.0.0.1:8008/health
echo Backend  : http://127.0.0.1:8010/api/health
echo Frontend : http://127.0.0.1:5173
echo ============================================================
echo.

start "" "http://127.0.0.1:5173"

echo You can close this launcher window now.
pause
