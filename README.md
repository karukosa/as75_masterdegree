# AS75 – nhận dạng nhiệt và tìm thông số PID

Firmware hiện chạy ở chế độ **thí nghiệm đáp ứng bước hở vòng**, thay cho chu
trình tiệt trùng tự động. Mục đích là ghi nhiệt độ theo thời gian tại ba mức
công suất để dựng biểu đồ, nhận dạng hàm truyền và lấy bộ thông số PID ban đầu.

## Cách chạy thí nghiệm

1. Nối ST-Link vào hai chân **SWDIO/SWCLK** đang dùng để nạp chương trình. Không
   cần chân SWO, UART hay thay đổi phần cứng.
2. Cấp nước và đóng cửa thủ công, sau đó chờ kiểm tra PT100 hoàn tất. Firmware
   thí nghiệm này bỏ qua hai tín hiệu cảm biến mức nước và công tắc cửa.
3. Nhấn **P1 = 100%**, **P2 = 70%**, hoặc **P3 = 40%**. Màn hình 1 hiện `H100`,
   `H 70`, hoặc `H 40`; màn hình 2 hiện nhiệt độ.
4. Nhấn **START**. Firmware xóa log cũ trong RAM, điều chế SSR theo cửa sổ 10
   giây, **ghi một mẫu mỗi 10 giây** và tự dừng sau **25 phút**. Nhấn START lần
   nữa để dừng sớm. Mảng `gHeaterTestLog` chứa tối đa 151 bản ghi (mẫu tại
   `t=0`, các mẫu định kỳ và một bản ghi trạng thái kết thúc), chiếm khoảng
   1,8 KiB RAM; trường `complete` đổi thành 1 sau khi dừng.
5. Để hệ thống nguội về cùng nhiệt độ ban đầu rồi mới chạy mức công suất kế
   tiếp. Nên lưu mỗi mức vào một file riêng (`step_100.csv`, `step_70.csv`,
   `step_40.csv`). Vì hai liên động đã bị vô hiệu hóa, người vận hành phải tự
   xác nhận đủ nước và cửa đã đóng chắc chắn trước khi nhấn START.

## Chuyển log RAM lên máy qua SWD

Thư mục `tools/` **không được nạp vào STM32**. Các file Python trong thư mục
này chỉ chạy trên máy tính sau khi đã lấy file dump RAM về. Phần thực sự được
biên dịch và nạp vào vi điều khiển gồm `Core/Src/main.c`,
`Core/Src/heater_test_log.c` và header `Core/Inc/heater_test_log.h`. Nếu đang
chép thay đổi sang một project STM32CubeIDE khác thay vì build trực tiếp repo
này, cần thêm `heater_test_log.c` vào nhóm `Core/Src`/danh sách source của
project; nếu không linker sẽ báo thiếu các hàm `HeaterTestLog_*`.

### Cách dễ nhất: để script tự mở GDB server

Cài OpenOCD và GNU Arm GDB, cắm cáp USB vào cổng **ST-LINK** của bo, đóng phiên
Debug đang mở trong STM32CubeIDE, rồi chạy lệnh sau **trước khi nhấn START**:

```bash
python3 tools/capture_ram_log.py \
  --start-openocd --elf build/firmware.elf --csv step_70.csv
```

Tùy chọn `--start-openocd` tự chạy lệnh tương đương:

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg
```

Khi thấy `Da ket noi SWD. Hay nhan START tren may.`, GDB server đã mở và có thể
nhấn START. Không mở đồng thời cửa sổ Debug của CubeIDE vì mỗi lúc chỉ nên có
một GDB client điều khiển bo. Nhấn `Ctrl+C` nếu muốn hủy chờ.

Nếu máy báo không tìm thấy chương trình, truyền đường dẫn cụ thể, ví dụ:

```bash
python3 tools/capture_ram_log.py --start-openocd \
  --openocd "C:/OpenOCD/bin/openocd.exe" \
  --gdb "C:/GNUArmEmbedded/bin/arm-none-eabi-gdb.exe" \
  --elf "Debug/ten_project.elf" --csv step_70.csv
```

### Nếu muốn tự mở server trong cửa sổ riêng

Terminal 1:

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg
```

Giữ Terminal 1 chạy. Khi thấy `Listening on port 3333 for gdb connections`, mở
Terminal 2 và chạy (không thêm `--start-openocd`):

```bash
python3 tools/capture_ram_log.py \
  --elf build/firmware.elf --csv step_70.csv
```

Script kết nối qua SWD, cho CPU chạy và chờ cờ `gHeaterTestLog.complete`. Sau
khi người vận hành nhấn START, toàn bộ các bước lấy mẫu 10 giây/lần, lưu RAM,
chờ đủ 25 phút (hoặc chờ lỗi/dừng sớm), halt CPU, dump RAM và tạo CSV đều tự
động. Không cần nhấn Pause hay nhập thêm lệnh GDB. Tùy chọn
`--keep-dump heater_ram.bin` sẽ giữ cả file nhị phân. Nếu GDB của STM32CubeIDE
không có trong `PATH`, truyền đường dẫn bằng `--gdb`.

STM32 không thể tự gửi dữ liệu chỉ qua dây SWD nếu trên máy tính không có một
GDB server và chương trình đang chờ nhận; vì vậy chỉ cần khởi chạy script một
lần trước thí nghiệm, sau đó thao tác duy nhất trên thiết bị là nhấn START.

### Trích xuất thủ công khi cần

Sau khi thí nghiệm dừng, **không reset hoặc ngắt nguồn bo** vì log nằm trong
RAM. Dùng GDB đi kèm STM32CubeIDE/OpenOCD, kết nối và halt CPU, rồi dump đúng
biến toàn cục (thay `build/firmware.elf` bằng file ELF thực tế):

```gdb
arm-none-eabi-gdb build/firmware.elf
(gdb) target extended-remote :3333
(gdb) monitor halt
(gdb) dump binary value heater_ram.bin gHeaterTestLog
(gdb) detach
```

Lưu ý: không dùng `monitor reset halt`, vì startup sẽ xóa RAM log nếu chạy
trước khi CPU được halt. Có thể dùng nút Pause của debugger thay cho
`monitor halt`. Nếu debugger không hỗ trợ `dump binary value`,
có thể dùng địa chỉ/kích thước mà GDB trả về từ `p &gHeaterTestLog` và
`p sizeof(gHeaterTestLog)` với lệnh `dump binary memory`.

Chuyển file nhị phân thành CSV:

```bash
python3 tools/extract_ram_log.py heater_ram.bin --csv step_70.csv
```

Script cũng chấp nhận một file dump toàn bộ SRAM vì nó tự tìm chữ ký
`HTLOG001`. CSV xuất ra có dạng:

```csv
elapsed_ms,power_percent,heater_on,temperature_c,status
1000,70,1,27.4,RUN
```

`heater_on` cho biết trạng thái SSR thực tế trong cửa sổ điều chế. Dù chỉ lưu
một mẫu mỗi 10 giây, firmware vẫn đọc PT100 mỗi 300 ms và ngắt thanh đốt khi
PT100 lỗi hoặc nhiệt độ vượt 138 °C; cảm biến mức nước và
công tắc cửa được bỏ qua để phục vụ riêng cho phép đo thủ công này. Có thể đặt
`WATER_CHECK_BYPASS_FOR_TEST` hoặc `DOOR_CHECK_BYPASS_FOR_TEST` về `0U` để bật
lại từng liên động.

## Lưu ý khi cập nhật project STM32CubeIDE

Repo vẫn giữ các file PID và USB do project CubeIDE đã sinh có thể còn liệt kê chúng
trong `Debug/*/subdir.mk`. Firmware thử nghiệm không gọi các module này, nhưng xóa file
khỏi repo mà chưa xóa chúng khỏi cấu hình build sẽ làm incremental build thất bại.

Khi chép mã vào project CubeIDE hiện có:

1. Chỉ thay các file tương ứng trong `Core/`; không chép thêm một bản `Core/` hoặc
   `USB_HOST/` vào bên trong `Middlewares/`.
2. Nếu Project Explorer đang có các source trùng như `Middlewares/Core/Src/main.c`
   hoặc `Middlewares/USB_HOST/Target/usbh_conf.c`, hãy xóa các bản trùng khỏi project.
3. Chọn **Project > Clean...**, sau đó **Refresh** và build lại. Việc clean sẽ tạo
   lại `subdir.mk`, tránh make tiếp tục biên dịch đường dẫn của file đã xóa hoặc di chuyển.

## Dựng biểu đồ, hàm truyền và PID

Chạy công cụ không cần thư viện Python ngoài:

```bash
python3 tools/analyze_heater_step.py step_70.csv \
  --svg step_70.svg --json step_70_model.json
```

Công cụ ước lượng mô hình bậc nhất có trễ (FOPDT):

```text
                  K exp(-L s)
G(s) =            -----------
                    T s + 1
```

Trong đó `K` là độ tăng °C trên một đơn vị công suất (1.0 = 100%), `L` là thời
gian trễ và `T` là hằng số thời gian. File SVG chồng đường đo và đường mô hình;
file JSON chứa hệ số hàm truyền và `pid_kp`, `pid_ki_per_s`, `pid_kd_s` theo
phương pháp IMC bảo thủ. Ba khóa bắt đầu bằng `firmware_pid_` đã được nhân 255
để dùng trực tiếp với thang đầu ra 0..255 khi khôi phục điều khiển PID. Đây chỉ là điểm khởi đầu: kiểm tra ở công suất thấp,
giới hạn đầu ra và giữ bảo vệ quá nhiệt khi đưa PID trở lại máy.
