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
rem Giu ten cu de tuong thich shortcut. Mac dinh do lap lai tai mot u.
rem Doi thanh step neu can dung lai phep do u1 den u2.
set "ANALYSIS_MODE=fixed"
rem live: tu luu CSV moi 10 phut qua TCL khi CPU van chay.
rem stop: cach cu, chi xuat CSV sau khi dung (mo truoc START hoac sau khi noi da dung).
set "CAPTURE_MODE=live"
set "CAPTURE_TOOL=capture_ram_log.py"
set "CAPTURE_OPTIONS="
if /i "%CAPTURE_MODE%"=="live" set "CAPTURE_TOOL=capture_live_ram_log.py"
if /i "%CAPTURE_MODE%"=="live" set "CAPTURE_OPTIONS=--autosave-seconds 600"
title AS75 - Lay mau manual

rem Project firmware STM32CubeIDE. Sua duong dan nay neu project duoc di chuyen.
set "PROJECT_DIR=C:\Users\Admin\Documents\Works\STM\Test_CIC\AS75_MD"
rem Thu muc chua file .bat nay cung la thu muc Python project.
set "PYTHON_PROJECT=%~dp0"
set "GDB=C:\ST\STM32CubeIDE_1.14.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.11.3.rel1.win32_1.1.100.202309141235\tools\bin\arm-none-eabi-gdb.exe"
set "ELF=%PROJECT_DIR%\Debug\AS75_MD.elf"
set "DATA_DIR=%PROJECT_DIR%\data"
set "ANALYZER=analyze_repeatability.py"
if /i "%ANALYSIS_MODE%"=="step" set "ANALYZER=analyze_heater_step.py"

echo ==============================================
echo AS75 - THI NGHIEM MANUAL, THANG 0-255
echo Che do phan tich: %ANALYSIS_MODE%
echo Che do lay du lieu: %CAPTURE_MODE%
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

if not exist "%PYTHON_PROJECT%tools\%CAPTURE_TOOL%" echo LOI: Khong tim thay tools\%CAPTURE_TOOL% trong %PYTHON_PROJECT%
if not exist "%PYTHON_PROJECT%tools\%CAPTURE_TOOL%" exit /b 6

if not exist "%PYTHON_PROJECT%tools\%ANALYZER%" echo LOI: Khong tim thay tools\%ANALYZER% trong %PYTHON_PROJECT%
if not exist "%PYTHON_PROJECT%tools\%ANALYZER%" exit /b 7

if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"
if errorlevel 1 echo LOI: Khong the tao thu muc du lieu: %DATA_DIR%
if errorlevel 1 exit /b 8

rem Moi lan chay co thu muc rieng, khong ghi de cac lan do truoc.
set "RUN_ID="
for /f "delims=" %%D in ('py -c "from datetime import datetime; print(datetime.now().isoformat().replace(':','').replace('-','').replace('.',''))"') do set "RUN_ID=%%D"
if not defined RUN_ID echo LOI: Khong tao duoc ten lan do.
if not defined RUN_ID exit /b 8
set "RUN_DIR=%DATA_DIR%\run_%RUN_ID%"
if exist "%RUN_DIR%" echo LOI: Thu muc lan do da ton tai: %RUN_DIR%
if exist "%RUN_DIR%" exit /b 8
mkdir "%RUN_DIR%"
if errorlevel 1 exit /b 8
set "CSV=%RUN_DIR%\manual_step.csv"
set "RAM=%RUN_DIR%\manual_ram.bin"
set "SVG=%RUN_DIR%\manual_step.svg"
set "JSON=%RUN_DIR%\manual_step_summary.json"
if /i "%ANALYSIS_MODE%"=="step" set "JSON=%RUN_DIR%\manual_step_model.json"
echo Du lieu lan nay: %RUN_DIR%

pushd "%PYTHON_PROJECT%"
if errorlevel 1 echo LOI: Khong the mo thu muc Python project: %PYTHON_PROJECT%
if errorlevel 1 exit /b 9

echo.
echo Da tim thay GDB, firmware va cong cu Python.
echo Dong cua so Debug STM32CubeIDE neu dang mo.
echo Khi bat dau lan moi: P1 tang u, P2 giam u; chon u roi nhan START.
if /i "%ANALYSIS_MODE%"=="fixed" echo Giu nguyen u ^(vi du u077^) trong ca lan do. Khong nhan P3 doi u.
if /i "%ANALYSIS_MODE%"=="fixed" echo Bat dau tu nhiet do moi truong, khong gia nhiet truoc.
if /i "%ANALYSIS_MODE%"=="fixed" echo Giu u den khi can bang, nhan START de dung va luu. Khong con tu dung sau 120 phut.
echo Firmware moi ghi 10 giay/mau, RAM 7204 ban ghi ^(khoang 20 gio^). Day RAM thi dung va giu log.
echo Dung ELF Debug trung voi firmware da nap. Tinh nang tu luu CSV nay khong can nap lai firmware.
echo Chu ky SSR van 10 giay; doc cam bien/bao ve moi 300 ms, nguong qua nhiet 138 do C.
if /i "%ANALYSIS_MODE%"=="step" echo Cho T1 on dinh, chon u2, nhan P3 mot lan roi cho T2 on dinh.
echo Luu log cu truoc khi reset hoac nhan START cho lan moi.
echo Neu log cu da hoan tat con trong RAM, script se lay lai log cu ngay khi ket noi.
echo Neu bat dau lan moi: luu log cu roi reset MCU truoc khi ket noi.
if /i "%CAPTURE_MODE%"=="live" echo Neu dang chay: khong reset hay nhan START lan moi; che do live doc tiep RAM dang co.
if /i "%CAPTURE_MODE%"=="stop" echo Che do stop chi ket noi truoc START hoac sau khi noi da dung, khong lay ban sao trong luc gia nhiet.
if /i "%CAPTURE_MODE%"=="live" echo CSV tu cap nhat moi 10 phut, them cac ban sao trong thu muc snapshots.
if /i "%CAPTURE_MODE%"=="live" echo Khong Pause/Halt CPU de sao luu. Khi dung noi, luu ban cuoi ngay.
if /i "%CAPTURE_MODE%"=="live" echo Dong cua so/Ctrl+C chi dung thu tren PC, khong tat gia nhiet tren noi.
echo.

py ".\tools\%CAPTURE_TOOL%" ^
  --start-openocd ^
  --gdb "%GDB%" ^
  --elf "%ELF%" ^
  --csv "%CSV%" ^
  --keep-dump "%RAM%" %CAPTURE_OPTIONS%

if errorlevel 1 goto :capture_failed

echo.
echo Da lay du lieu. Dang ve do thi va phan tich theo che do %ANALYSIS_MODE%...
if /i "%ANALYSIS_MODE%"=="fixed" echo Xet 10 phut cuoi: dai nhiet khong qua 1 do C, tri tuyet doi do troi khong qua 0.05 do C/phut.
py ".\tools\%ANALYZER%" "%CSV%" --svg "%SVG%" --json "%JSON%"
if errorlevel 1 goto :analysis_failed

popd
echo.
if /i "%ANALYSIS_MODE%"=="fixed" echo DA XUAT DO THI VA THONG KE. Xem do doc va dai nhiet cuoi; chua dat van la ket qua do hop le.
if /i "%ANALYSIS_MODE%"=="step" echo DA XUAT KET QUA NHAN DANG. Xem cac dieu kien va canh bao trong JSON.
echo CSV: %CSV%
echo RAM: %RAM%
echo DO THI: %SVG%
echo THONG KE JSON: %JSON%
if /i "%ANALYSIS_MODE%"=="fixed" echo BANG THONG KE: %RUN_DIR%\manual_step_summary.csv
exit /b 0

:capture_failed
popd
echo.
echo CO LOI THU DU LIEU: Khong thuc hien phan tich.
echo Kiem tra CSV va snapshots da luu tai %RUN_DIR%. Noi co the van dang chay; kiem tra tren may.
exit /b 10

:analysis_failed
popd
echo.
echo CO LOI PHAN TICH: Xem thong bao phia tren. CSV va RAM van duoc giu tai %RUN_DIR%.
if /i "%ANALYSIS_MODE%"=="fixed" echo Che do fixed yeu cau giu nguyen mot u trong ca lan do.
if /i "%ANALYSIS_MODE%"=="step" echo Kiem tra mot buoc tang u va do on dinh truoc/sau buoc.
exit /b 11
