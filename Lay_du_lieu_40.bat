@echo off
setlocal

rem Luon quay lai day de giu cua so mo, ke ca khi :main gap loi.
call :main
set "RESULT=%errorlevel%"
echo.
if "%RESULT%"=="0" echo Hoan tat. Nhan phim bat ky de dong cua so.
if not "%RESULT%"=="0" echo Chuong trinh gap loi ^(ma %RESULT%^). Doc thong bao phia tren.
pause >nul
exit /b %RESULT%

:main
rem Chi can doi gia tri nay thanh 0, 40, 70 hoac 100 cho muc cong suat can thu.
set "POWER_PERCENT=0"
set "POWER_BUTTON="

if "%POWER_PERCENT%"=="100" set "POWER_BUTTON=P1"
if "%POWER_PERCENT%"=="70" set "POWER_BUTTON=P2"
if "%POWER_PERCENT%"=="40" set "POWER_BUTTON=P3"
if "%POWER_PERCENT%"=="0" set "POWER_BUTTON=P3"
if not defined POWER_BUTTON echo LOI: POWER_PERCENT chi duoc phep la 0, 40, 70 hoac 100.
if not defined POWER_BUTTON exit /b 2

title AS75 - Lay va phan tich du lieu %POWER_PERCENT% phan tram

rem Project firmware STM32CubeIDE. Sua duong dan nay neu project duoc di chuyen.
set "PROJECT_DIR=C:\Users\Admin\Documents\Works\STM\Test_CIC\AS75_MD"
rem Thu muc chua file .bat nay cung la thu muc Python project.
set "PYTHON_PROJECT=%~dp0"
set "GDB=C:\ST\STM32CubeIDE_1.14.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.11.3.rel1.win32_1.1.100.202309141235\tools\bin\arm-none-eabi-gdb.exe"
set "ELF=%PROJECT_DIR%\Debug\AS75_MD.elf"
set "DATA_DIR=%PROJECT_DIR%\data"
set "CSV=%DATA_DIR%\step_%POWER_PERCENT%.csv"
set "RAM=%DATA_DIR%\heater_ram_%POWER_PERCENT%.bin"
set "SVG=%DATA_DIR%\step_%POWER_PERCENT%.svg"
set "JSON=%DATA_DIR%\step_%POWER_PERCENT%_model.json"

echo ==============================================
echo AS75 - THI NGHIEM VA PHAN TICH CONG SUAT %POWER_PERCENT% PHAN TRAM
echo ==============================================
echo.
echo Kiem tra cac file can thiet...

where py >nul 2>nul
if errorlevel 1 echo LOI: Khong tim thay Python Launcher ^(py^) trong PATH.
if errorlevel 1 exit /b 3

if not exist "%GDB%" echo LOI: Khong tim thay GDB: %GDB%
if not exist "%GDB%" exit /b 4

if not exist "%ELF%" echo LOI: Khong tim thay firmware ELF: %ELF%
if not exist "%ELF%" echo Hay Build Project trong STM32CubeIDE.
if not exist "%ELF%" exit /b 5

if not exist "%PYTHON_PROJECT%tools\capture_ram_log.py" echo LOI: Khong tim thay tools\capture_ram_log.py trong %PYTHON_PROJECT%
if not exist "%PYTHON_PROJECT%tools\capture_ram_log.py" exit /b 6

if not exist "%PYTHON_PROJECT%tools\analyze_heater_step.py" echo LOI: Khong tim thay tools\analyze_heater_step.py trong %PYTHON_PROJECT%
if not exist "%PYTHON_PROJECT%tools\analyze_heater_step.py" exit /b 7

if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"
if errorlevel 1 echo LOI: Khong the tao thu muc du lieu: %DATA_DIR%
if errorlevel 1 exit /b 8

pushd "%PYTHON_PROJECT%"
if errorlevel 1 echo LOI: Khong the mo thu muc Python project: %PYTHON_PROJECT%
if errorlevel 1 exit /b 9

echo.
echo Da tim thay GDB, firmware va cong cu Python.
echo Dong cua so Debug STM32CubeIDE neu dang mo.
if "%POWER_PERCENT%"=="0" echo Khi ket noi SWD, nhan P3 den khi man hinh hien H  0, roi nhan START.
if not "%POWER_PERCENT%"=="0" echo Khi ket noi SWD, chon %POWER_BUTTON% ^(%POWER_PERCENT% phan tram^) va nhan START.
echo.

py ".\tools\capture_ram_log.py" ^
  --start-openocd ^
  --gdb "%GDB%" ^
  --elf "%ELF%" ^
  --csv "%CSV%" ^
  --keep-dump "%RAM%"

if errorlevel 1 goto :capture_failed
if "%POWER_PERCENT%"=="0" goto :cooling_complete

echo.
echo Da lay du lieu. Dang kiem tra va nhan dang mo hinh FOPDT...
py ".\tools\analyze_heater_step.py" "%CSV%" --svg "%SVG%" --json "%JSON%"
if errorlevel 1 goto :analysis_failed

popd
echo.
echo KET LUAN: DU LIEU CO THE DUNG DE TINH BO PID BAN DAU.
echo CSV: %CSV%
echo RAM: %RAM%
echo DO THI: %SVG%
echo MO HINH VA PID: %JSON%
echo.
echo CANH BAO: Thong so PID chi la diem khoi dau. Can thu o cong suat thap,
echo gioi han dau ra va luon giu bao ve qua nhiet khi dua vao firmware.
exit /b 0

:cooling_complete
echo.
echo Da lay du lieu. Dang ve do thi va nhan dang mo hinh nguoi...
py ".\tools\analyze_cooling.py" "%CSV%" --svg "%SVG%" --json "%JSON%"
if errorlevel 1 goto :analysis_failed

popd
echo.
echo DA LAY XONG VA PHAN TICH DU LIEU NGUOI 0 PHAN TRAM.
echo CSV: %CSV%
echo RAM: %RAM%
echo DO THI: %SVG%
echo MO HINH NGUOI: %JSON%
echo LUU Y: Can them log gia nhiet de tinh PID.
exit /b 0

:capture_failed
popd
echo.
echo CO LOI: Khong lay duoc du lieu. Khong thuc hien phan tich PID.
exit /b 10

:analysis_failed
popd
echo.
echo KET LUAN: DU LIEU CHUA THE DUNG DE TINH PID.
echo Xem loi phan tich phia tren, sau do kiem tra cam bien va chay lai du 35 phut.
exit /b 11
