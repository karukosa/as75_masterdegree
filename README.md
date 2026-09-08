# AS75 – nhận dạng nhiệt và tìm thông số PID

Firmware hiện chạy ở chế độ **thí nghiệm đáp ứng bước hở vòng**, thay cho chu
trình tiệt trùng tự động. Mục đích là ghi nhiệt độ theo thời gian tại ba mức
công suất để dựng biểu đồ, nhận dạng hàm truyền và lấy bộ thông số PID ban đầu.

## Cách chạy thí nghiệm

1. Nối ST-Link vào hai chân **SWDIO/SWCLK** đang dùng để nạp chương trình. Không
   cần chân SWO, UART hay thay đổi phần cứng.
2. Chờ kiểm tra PT100, nước và cửa hoàn tất.
3. Nhấn **P1 = 100%**, **P2 = 70%**, hoặc **P3 = 40%**. Màn hình 1 hiện `H100`,
   `H 70`, hoặc `H 40`; màn hình 2 hiện nhiệt độ.
4. Nhấn **START**. Firmware xóa log cũ trong RAM, điều chế SSR theo cửa sổ 10
   giây, lấy mẫu mỗi giây và tự dừng sau 45 phút. Nhấn START lần nữa để dừng
   sớm. Mảng `gHeaterTestLog` chứa tối đa 2702 mẫu (khoảng 32 KiB); trường
   `complete` đổi thành 1 sau khi dừng.
5. Để hệ thống nguội về cùng nhiệt độ ban đầu rồi mới chạy mức công suất kế
   tiếp. Nên lưu mỗi mức vào một file riêng (`step_100.csv`, `step_70.csv`,
   `step_40.csv`). Không vận hành thiết bị nếu các liên động an toàn chưa được
   kiểm chứng trên phần cứng thực.

## Chuyển log RAM lên máy qua SWD

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

`heater_on` cho biết trạng thái SSR thực tế trong cửa sổ điều chế. Firmware vẫn
ngắt khi cửa mở, lỗi PT100 hoặc nhiệt độ vượt 138 °C.

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
để dùng với thư viện PID hiện tại (đầu ra 0..255). Đây chỉ là điểm khởi đầu: kiểm tra ở công suất thấp,
giới hạn đầu ra và giữ bảo vệ quá nhiệt khi đưa PID trở lại máy.
