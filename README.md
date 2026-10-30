# AS75 – lấy mẫu manual và so sánh các lần gia nhiệt

Firmware thực hiện thí nghiệm hở vòng, ghi nhiệt độ PT100 thực theo thời gian.
Có thể **giữ một u cố định trong cả lần đo** để kiểm tra độ lặp lại, hoặc thực
hiện bước u1 → u2 để dùng công cụ nhận dạng riêng. Thang u là **0–255**.
Firmware này không chạy PID hoặc tự giữ nhiệt độ; người vận hành chọn u.

Cách đo hiện tại: chạy khoảng **5 lần tại u=77 (30,20%)**, cùng điều kiện ban
đầu và tải, rồi so sánh các đường nhiệt độ. Không cần đổi sang u2. Chỉ cập nhật
công cụ trên máy tính cho cách phân tích này, **không cần nạp lại firmware**.

## Bảng chân để tạo project mới

Bảng dưới đây được đối chiếu trực tiếp với `main.h`, `MX_GPIO_Init()` và
`HAL_SPI_MspInit()`. Tên chân ở cột đầu nên được đặt lại đúng như bảng trong
STM32CubeMX/CubeIDE; khi đó có thể chép các file chương trình đã lưu mà không
phải sửa lại tên chân trong mã nguồn.

### Các chân firmware thí nghiệm đang thực sự dùng

| Nhãn trong code | Chân MCU | Cấu hình CubeMX | Chức năng / mức tác động |
|---|---|---|---|
| `B_P1` | PC0 | GPIO Input, No pull | Nút tăng u, tác động mức cao |
| `B_P2` | PC1 | GPIO Input, No pull | Nút giảm u, tác động mức cao |
| `B_P3` | PC2 | GPIO Input, No pull | Nút áp dụng u, tác động mức cao |
| `B_Start` | PC6 | GPIO Input, No pull | Nút chạy/dừng, tác động mức cao |
| `LD_P1` | PD2 | GPIO Output, Push-pull, Low speed, No pull | LED nút tăng u, bật mức cao |
| `LD_P2` | PD1 | GPIO Output, Push-pull, Low speed, No pull | LED nút giảm u, bật mức cao |
| `LD_P3` | PD0 | GPIO Output, Push-pull, Low speed, No pull | LED nút áp dụng u, bật mức cao |
| `LD_Start` | PD6 | GPIO Output, Push-pull, Low speed, No pull | LED đang chạy, bật mức cao |
| `SSR_Heater` | PE10 | GPIO Output, Push-pull, Low speed, No pull | SSR thanh đốt, bật mức cao |
| `Buzzer` | PB10 | GPIO Output, Push-pull, Low speed, No pull | Còi, bật mức cao |
| `CLK1` / `DIO1` | PB6 / PB7 | GPIO Output Open-drain, Pull-up, Very high speed | Màn hình TM1637 số 1 (giá trị u đặt trước) |
| `CLK2` / `DIO2` | PB8 / PB9 | GPIO Output Open-drain, Pull-up, Very high speed | Màn hình TM1637 số 2 (nhiệt độ) |
| `CS` | PA4 | GPIO Output, Push-pull, Low speed, No pull | Chip-select MAX31865, phần mềm điều khiển |
| SPI3 SCK / MISO / MOSI | PB3 / PB4 / PB5 | AF6 SPI3, Push-pull, Very high speed, No pull | Giao tiếp MAX31865/PT100 |
| `BOOT1` | PB2 | GPIO Input, No pull | Chân khởi động mặc định của bo mạch, giữ nguyên cấu hình |
| SWDIO / SWCLK | PA13 / PA14 | SYS Debug: Serial Wire | Nạp, debug và đọc log RAM qua ST-Link |
| OSC_IN / OSC_OUT | PH0 / PH1 | RCC HSE Crystal/Ceramic Resonator | Thạch anh HSE 8 MHz |

SPI3 phải đặt ở chế độ **Master, Full-Duplex, 8-bit, CPOL Low, CPHA 2nd
Edge, Software NSS, MSB first, prescaler 128**. Clock dùng HSE 8 MHz và PLL
`M=8, N=336, P=2, Q=7`, tạo SYSCLK 168 MHz; APB1 chia 4 và APB2 chia 2.

`MX_GPIO_Init()` chỉ khởi tạo các GPIO phục vụ phép đo trong bảng và `BOOT1`
mặc định. Các nút, LED, cảm biến nước/cửa, SSR phụ, relay và bơm không tham gia
phép đo đã được loại khỏi mã nguồn. Các nhãn USB OTG FS và LSE không dùng cũng
đã được xóa khỏi `main.h` để danh sách chân khớp với firmware thí nghiệm.

### Chép chương trình sang project mới

1. Tạo project cho đúng **STM32F407VGTx**, cấu hình pin, clock và SPI3 theo các
   bảng trên rồi sinh code HAL.
2. Chép các file đã lưu trong `Core/Src`: `main.c`, `button_input.c`,
   `tm1637.c`, `max31865.c`, `heater_test_log.c` và các header cùng tên trong
   `Core/Inc`. Đảm bảo `heater_test_log.c` được đưa vào danh sách source build.
3. Cách ít sai nhất là thay toàn bộ `main.c` sau khi đã lưu bản CubeMX vừa
   sinh. Nếu muốn giữ `main.c` mới, phải chuyển thủ công cả phần khai báo, các
   hàm ứng dụng và nội dung vòng lặp vào vùng `USER CODE` tương ứng; file hiện
   tại không bao toàn bộ logic ứng dụng trong các vùng đó. Giữ nguyên tên
   label pin trong bảng và không chép đè `stm32f4xx_hal_msp.c` nếu đã cấu hình
   SPI3 bằng CubeMX.
4. Không cần chép thư mục `tools/` vào firmware. Thư mục này chỉ chạy trên máy
   tính để lấy và phân tích log.

## Đo lặp lại tại một u cố định (cách hiện tại)

1. Giữ cùng lượng nước/tải, trạng thái cửa nồi, vị trí PT100, cách gia nhiệt
   trước và nhiệt độ bắt đầu giữa các lần đo. Cùng số đo PT100 ban đầu chưa
   bảo đảm thành nồi có cùng trạng thái tích nhiệt.
2. Lưu xong lần trước trước khi reset hoặc START lại. Nếu log cũ đã hoàn tất
   còn trong RAM, script capture sẽ lấy lại log đó ngay khi kết nối. Để chuẩn
   bị lần mới sau khi đã lưu log cũ, reset MCU, chọn u077 bằng P1/P2, mở script
   capture rồi nhấn START. Giữ u077 suốt lần đo; không nhấn P3 đổi u.
3. Chọn cùng thời gian ghi trong phạm vi cho phép của thí nghiệm. Nhấn START
   để dừng/lưu; firmware cũng tự hoàn tất khi hết 120 phút. Các bảo vệ sẵn có
   vẫn hoạt động. Không coi DONE là xác nhận nhiệt độ ổn định.
4. Lặp khoảng 5 lần, lưu riêng CSV, RAM và biểu đồ. `Lay_du_lieu_40.bat`
   **giữ tên cũ nhưng mặc định phân tích một u cố định** (`ANALYSIS_MODE=fixed`).
   Chỉnh `PROJECT_DIR`, `GDB` trong file cho đúng máy. Mỗi lần mở `.bat`,
   kết quả lưu vào `%PROJECT_DIR%\data\run_<ngày giờ>\` gồm:
   `manual_step.csv`, `manual_ram.bin`, `manual_step.svg`,
   `manual_step_summary.json`, `manual_step_summary.csv`.
   Những file cũ trực tiếp trong `data` không bị ghi đè.
5. Dùng công cụ bên dưới để so sánh các CSV đã chọn. Chỉ đưa vào cùng nhóm
   các lần chạy cùng u, tránh trộn các mức 60–80 hoặc các lần khác cách chuẩn
   bị. CSV có bước đổi u bị từ chối ở chế độ phân tích này.

## Chuẩn bị và tìm u1/u2 (khi cần phép đo bước)

1. Chọn vùng nhiệt độ làm việc và giới hạn nhiệt độ cho phép của thí nghiệm.
   Giữ nguyên lượng nước/tải, cửa và điều kiện môi trường giữa hai pha.
   Nếu có số u của PID khi giữ ổn định ở vùng đó, dùng làm điểm tham khảo;
   firmware hiện tại chỉ cung cấp manual.
2. Xác nhận đủ nước và cửa đóng. Firmware không đọc liên động nước/cửa.
   Bảo vệ PT100 và ngắt khi số đo chưa qua lọc đạt **138 °C** vẫn hoạt động;
   đây không thay thế giới hạn riêng của thí nghiệm.
3. Dò công suất bằng các nút bên dưới. Khởi động mặc định **u=0**. Bắt đầu
   thấp, tăng từng bước phù hợp rồi chờ đủ lâu. Chưa có số liệu để ấn định
   u1/u2 hay bảo đảm một u cụ thể sẽ an toàn/đạt xác lập.
4. Chọn **u2 > u1**, sao cho cả T1 và T2 cân bằng nằm trong vùng cho phép và
   chênh nhiệt đủ lớn so với nhiễu. Nhiệt độ còn tăng không tự chứng minh
   u quá cao: có thể chưa chờ đủ lâu, tải/điều kiện thay đổi hoặc lỗi đo.
5. Kết thúc lần dò, lưu nếu cần, rồi bắt đầu lần đo chính thức ở u1. Lần đo
   chính thức chỉ có **một bước u1 → u2** để phân tích tự động rõ ràng.

## Thao tác nút và lấy mẫu

| Nút | Khi dừng | Khi đang chạy |
|---|---|---|
| P1 | Tăng u đặt trước 1 đơn vị | Chỉ tăng u đặt trước; chưa đổi SSR |
| P2 | Giảm u đặt trước 1 đơn vị | Chỉ giảm u đặt trước; chưa đổi SSR |
| P3 | Không gia nhiệt | Chốt u đặt trước để áp dụng ở đầu chu kỳ 10 s kế tiếp |
| START | Xóa log cũ, chạy manual tại u đặt trước | Tắt SSR, ghi trạng thái STOP, hoàn tất log |

Giữ P1/P2 sẽ lặp sau 1 giây, mỗi 0,5 giây một đơn vị. Màn hình 1 hiện
`u000`…`u255`; dấu chấm ở chữ số cuối sáng khi giá trị đặt trước khác u đang
áp dụng hoặc có lệnh đang chờ. P3 chốt giá trị tại thời điểm bấm; chỉnh P1/P2
sau đó không thay lệnh đã chốt, trừ khi bấm P3 lại. LED P1/P2/P3 chỉ nút vừa
thao tác; LED START cho biết đang chạy.

Quy trình dưới đây dành riêng cho phép đo bước u1 → u2. Đo lặp lại một u dùng
quy trình phía trên, không cần P3:

1. Chạy script lấy log trên máy tính trước (xem dưới). Đặt u1 bằng P1/P2,
   nhấn START để bắt đầu ghi cả quá trình tiến đến T1.
2. Giữ u1 đến khi nhiệt độ ổn định. Không cần đường nhiệt nằm ngang tuyệt đối:
   xét cả biên độ dao động và xu hướng trong một khoảng đủ dài. Mặc định tool
   kiểm tra **ít nhất 5 phút**, biên max–min **≤1 °C**, độ dốc hồi quy
   **≤0,05 °C/phút** theo trị tuyệt đối. Đây là ngưỡng khởi đầu cần điều chỉnh
   theo yêu cầu thực tế, không phải thông số vật lý đã xác nhận của lò.
3. Chỉnh u2 bằng P1/P2 (lò vẫn chạy u1), nhấn P3 đúng một lần. Firmware giữ
   nguyên mốc cửa sổ SSR và đổi u ở đầu cửa sổ kế tiếp; log thêm sự kiện
   `POWER_CHANGE` theo thời điểm **áp dụng thực tế**, không theo thời điểm bấm.
4. Giữ nguyên u2 đến khi T2 ổn định trong cùng tiêu chí. Tiếp tục ghi đủ đoạn
   cuối xác lập; T2 rất quan trọng nhưng vẫn phải giữ T1 và Δu để tính độ lợi.
5. Nhấn START để dừng và tự xuất RAM/CSV qua SWD. Không reset/mất nguồn trước
   khi lấy log; không nhấn START lần nữa vì sẽ xóa lần đo vừa xong.

Chu kỳ SSR luôn là **ton + toff = 10 s**, với
`ton = floor(10000 × u / 255) ms`, `toff = 10000 − ton`.
Chu kỳ lấy mẫu độc lập: danh định **1 s/mẫu**, kiểm tra/đọc PT100 mỗi **300 ms**.
Do lịch đọc 300 ms, khoảng cách mẫu thực có thể khoảng 0,9–1,2 s; dùng
`elapsed_ms` thực tế để vẽ/khớp mô hình. MAX31865 chạy chuyển đổi liên tục,
giảm chặn vòng lặp do chờ one-shot. Điều chế dùng vòng lặp phần mềm nên có
sai lệch nhỏ do SPI/hiển thị; cần xác nhận trên bo nếu yêu cầu thời gian chặt.

Log lưu **nhiệt độ đo chưa qua bộ lọc hiển thị** (vẫn có hiệu chỉnh -5 °C vốn
có trong driver, không phải giá trị ADC thô). Màn hình dùng nhiệt độ đã lọc;
bảo vệ quá nhiệt dùng nhiệt độ chưa qua lọc để tránh trễ do lọc phần mềm.

Không tự kết thúc sau 35 phút. Log có 7204 bản ghi, chiếm **86.472 byte RAM**,
giới hạn tối đa **120 phút cho cả lần đo**; nhiều sự kiện đổi u có thể làm đầy
sớm hơn. Khi đầy, timeout, dừng tay hoặc lỗi, firmware tắt SSR rồi hoàn tất log.
`DONE`/timeout **không có nghĩa** nhiệt độ đã xác lập. Không kết luận xác lập
hay hàm truyền từ việc hết giờ. STM32F407VG có
128 KiB SRAM thông thường, nhưng phải kiểm tra báo cáo RAM của project CubeIDE
thực tế khi thêm log vào chương trình khác.

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
  --start-openocd --elf build/firmware.elf --csv manual_step.csv
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
  --elf "Debug/ten_project.elf" --csv manual_step.csv
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
  --elf build/firmware.elf --csv manual_step.csv
```

Script kết nối qua SWD, cho CPU chạy và chờ cờ `gHeaterTestLog.complete`. Sau
khi người vận hành nhấn START, toàn bộ các bước lấy mẫu danh định 1 giây/lần, lưu RAM,
chờ nhấn START để dừng (hoặc đạt giới hạn 120 phút/bộ nhớ),
halt CPU, dump RAM và tạo CSV đều tự
động. Không cần nhấn Pause hay nhập thêm lệnh GDB. Tùy chọn
`--keep-dump heater_ram.bin` sẽ giữ cả file nhị phân. Nếu GDB của STM32CubeIDE
không có trong `PATH`, truyền đường dẫn bằng `--gdb`.

Các thư mục cha trong đường dẫn `--csv` và `--keep-dump` sẽ được tạo tự động.
Vì vậy có thể dùng trực tiếp, ví dụ `--csv data/manual_step.csv --keep-dump
data/manual_ram.bin`, kể cả khi thư mục `data` chưa tồn tại. Điều này cũng
đảm bảo GDB ghi được dump khi nhấn START lần nữa để dừng thí nghiệm sớm.
Trên Windows, script tự chuyển đường dẫn gửi cho GDB sang dấu `/`; file `.bat`
vẫn có thể truyền đường dẫn bằng dấu `\\` như bình thường.
Để tương thích cả các bản GDB không ghi được đường dẫn Windows tuyệt đối,
script yêu cầu GDB ghi tên ngắn không có dấu nháy (`heater_ram.bin`) trong một
thư mục tạm có sẵn, sau đó dùng Python sao chép file sang đường dẫn
`--keep-dump`. Python tạo sẵn file rỗng này để tương thích với các bản
xPack GDB trên Windows không tự tạo được file đích của lệnh `dump binary`.

Log phiên bản 2 có kích thước **86.472 byte**, lớn hơn giới hạn giá trị 64 KiB
thường gặp của GDB. Script đặt `set max-value-size unlimited` trước khi đọc log
và dùng `dump binary memory` để lấy trực tiếp vùng nhớ. Nếu gặp lỗi
`requires 86472 bytes, which is more than max-value-size`, cập nhật
`tools/capture_ram_log.py` trên máy tính rồi chạy lại `.bat`; không cần đổi hoặc
nạp lại firmware chỉ để sửa lỗi GDB này.

Nếu lần lấy dữ liệu trước bị lỗi nhưng bo vẫn có nguồn và log đã hoàn tất,
script sẽ tự lấy lại log ngay khi kết nối. **Không reset bo hoặc nhấn START
chạy lần mới**, vì sẽ xóa dữ liệu trong RAM. Nếu phép đo chưa hoàn tất, script
tiếp tục chờ như bình thường.

STM32 không thể tự gửi dữ liệu chỉ qua dây SWD nếu trên máy tính không có một
GDB server và chương trình đang chờ nhận; vì vậy chỉ cần khởi chạy script một
lần trước thí nghiệm, sau đó chỉnh u, áp dụng bước bằng P3 và chạy/dừng bằng START.

### Trích xuất thủ công khi cần

Sau khi thí nghiệm dừng, **không reset hoặc ngắt nguồn bo** vì log nằm trong
RAM. Dùng GDB đi kèm STM32CubeIDE/OpenOCD, kết nối và halt CPU, rồi dump đúng
biến toàn cục (thay `build/firmware.elf` bằng file ELF thực tế):

```gdb
arm-none-eabi-gdb build/firmware.elf
(gdb) set max-value-size unlimited
(gdb) target extended-remote :3333
(gdb) monitor halt
(gdb) dump binary memory heater_ram.bin &gHeaterTestLog ((char *)&gHeaterTestLog + sizeof(gHeaterTestLog))
(gdb) detach
```

Lưu ý: không dùng `monitor reset halt`, vì startup sẽ xóa RAM log nếu chạy
trước khi CPU được halt. Có thể dùng nút Pause của debugger thay cho
`monitor halt`. Lệnh `dump binary memory` lấy địa chỉ và kích thước từ symbol
trong ELF; cần dùng đúng file ELF khớp với firmware đã nạp trên bo.

Chuyển file nhị phân thành CSV:

```bash
python3 tools/extract_ram_log.py heater_ram.bin --csv manual_step.csv
```

Script cũng chấp nhận một file dump toàn bộ SRAM vì nó tự tìm chữ ký
`HTLOG001`. CSV firmware mới có dạng:

```csv
elapsed_ms,power_percent,heater_on,temperature_c,status,control_u,control_u_max
0,20,1,80.1,RUN,51,255
600003,24,1,80.2,POWER_CHANGE,61,255
600105,24,1,80.1,RUN,61,255
```

`power_percent` được làm tròn để đọc nhanh; nhận dạng dùng chính xác
`control_u/control_u_max`. `heater_on` là trạng thái SSR tại bản ghi, không phải
công suất trung bình. `POWER_CHANGE.elapsed_ms` đánh dấu thời điểm firmware
áp dụng u2; nhiệt độ trong bản ghi sự kiện là số đo gần nhất (có thể cũ tối đa
khoảng 300 ms). Các mẫu `RUN` ghi thời điểm đọc cảm biến thực tế, không nội suy
hay điền lại những mẫu bị lỡ. Mẫu kết thúc phản ánh SSR đã tắt; nếu lỗi cảm biến,
nhiệt độ trong bản ghi kết thúc là số đo hợp lệ gần nhất.

Tool vẫn giải mã được log nhị phân phiên bản 1 và CSV cũ. Log mới là phiên bản
2, bản ghi vẫn 12 byte; không dùng tool cũ để đọc vì sẽ mất giá trị u chính xác.

## Lưu ý khi cập nhật project STM32CubeIDE

Project CubeIDE cũ có thể còn liệt kê các module PID/USB trong
`Debug/*/subdir.mk`. Cần cập nhật cấu hình build nếu các file đó đã bị loại bỏ;
firmware manual hiện tại không dùng các module này.

Khi chép mã vào project CubeIDE hiện có:

1. Chỉ thay các file tương ứng trong `Core/`; không chép thêm một bản `Core/` hoặc
   `USB_HOST/` vào bên trong `Middlewares/`.
2. Nếu Project Explorer đang có các source trùng như `Middlewares/Core/Src/main.c`
   hoặc `Middlewares/USB_HOST/Target/usbh_conf.c`, hãy xóa các bản trùng khỏi project.
3. Chọn **Project > Clean...**, sau đó **Refresh** và build lại. Việc clean sẽ tạo
   lại `subdir.mk`, tránh make tiếp tục biên dịch đường dẫn của file đã xóa hoặc di chuyển.

## Đồ thị một u cố định và so sánh độ lặp lại

Chạy ở thư mục gốc repo. Trên Windows dùng `py`; trên Linux dùng `python3`.
Công cụ chỉ cần thư viện chuẩn Python, không cần cài matplotlib.

Vẽ và thống kê một lần (nhiệt độ đầu/cuối RUN, min/max, thời gian, u, trung
bình, max−min và độ dốc hồi quy cửa sổ cuối):

```bat
py tools\analyze_repeatability.py data\u77_lan1.csv
```

Đầu ra mặc định cùng thư mục CSV: `u77_lan1.svg`, `u77_lan1_summary.json`,
`u77_lan1_summary.csv`. Không cần bước đổi u, không tự khớp hàm truyền hoặc
tính PID. Nhiệt độ chưa ổn định vẫn là kết quả đo hợp lệ, không báo lỗi chỉ
vì không đạt tiêu chí cuối. Log lỗi cảm biến/đầy RAM/chưa hoàn tất được ghi
cảnh báo riêng, không xác nhận cửa sổ cuối hợp lệ.

Đồ thị một lần và biểu đồ chồng mặc định dùng **thang nhiệt 0–140 °C** để
trình bày tổng quan, tránh tự phóng to khoảng nhiệt nhỏ làm đường nhìn quá
dốc. Không lọc hay thay đổi số đo, thời gian hoặc độ dốc tính toán. Thang tự
mở rộng nếu có số đo nằm ngoài để không che dữ liệu. Dùng cùng thang khi so
sánh các lần; JSON ghi khoảng trục thực tế trong `chart_temperature_range_c`.
Có thể đổi bằng `--temperature-range-c 0 150`, hoặc xem bản phóng to như trước:

```bat
py tools\analyze_repeatability.py data\u77_lan1.csv --auto-temperature-range --svg data\u77_lan1_zoom.svg --json data\u77_lan1_zoom.json --summary-csv data\u77_lan1_zoom_summary.csv
```

Chồng 5 đường từ các file đã đặt tên `u77_lan1.csv`…`u77_lan5.csv`:

```bat
py tools\analyze_repeatability.py "data\u77_lan*.csv" --svg data\u77_so_sanh.svg --json data\u77_so_sanh.json --summary-csv data\u77_so_sanh.csv
```

Hoặc dùng đường dẫn tuyệt đối nơi `.bat` lưu dữ liệu, chọn các thư mục cùng
u77; ví dụ nếu tất cả `run_*` dưới đây đều là u77:

```bat
py tools\analyze_repeatability.py "C:\Users\Admin\Documents\Works\STM\Test_CIC\AS75_MD\data\run_*\manual_step.csv" --svg data\u77_so_sanh.svg --json data\u77_so_sanh.json --summary-csv data\u77_so_sanh.csv
```

Cũng có thể liệt kê riêng từng đường dẫn CSV, phân cách bằng khoảng trắng.
Tool hỗ trợ mẫu tên file trong dấu ngoặc kép ngay trên Windows CMD. Không
chọn cùng một CSV hai lần; các mức u khác nhau phải phân tích thành nhóm riêng.

Biểu đồ chồng giữ nguyên nhiệt độ tuyệt đối và thời gian từ đầu log, có màu
và tooltip riêng cho mỗi lần; không dịch đường để làm trùng nhiệt độ đầu.
Bảng cuối từng lần ghi cả thời gian kết thúc vì các cửa sổ cuối có thể ở
những thời điểm khác nhau. Chỉ số giữa các lần chỉ dùng **khoảng thời gian
chung**: nội suy tuyến tính mỗi 10 s, không ngoại suy sau khi một lần đã dừng,
bỏ mốc nằm trong khoảng trống lấy mẫu dài. JSON có RMSE từng cặp, trung bình/
max chênh nhiệt giữa các lần và trung bình độ lệch chuẩn mẫu giữa các lần.
Không tự gắn nhãn độ lặp lại đạt/không đạt khi chưa có ngưỡng thống nhất.

Tiêu chí cửa sổ cuối mặc định: 300 s, max−min ≤1 °C, |độ dốc| ≤0,05 °C/phút.
Có thể đổi bằng `--steady-window-s`, `--steady-band-c`,
`--steady-slope-c-per-min` theo yêu cầu GV. Đạt một cửa sổ chưa chứng minh cân
bằng lâu dài; nhiều đường lặp lại tốt vẫn có thể cùng đang tăng nhiệt.

## Đồ thị và hàm truyền từ phép đo bước (công cụ riêng)

Nếu dùng lại phép đo bước, đổi `ANALYSIS_MODE=step` trong `.bat`, hoặc gọi
trực tiếp công cụ dưới đây. Công cụ này vẫn yêu cầu đúng một bước u1 → u2;
không dùng nó để đánh giá 5 lần giữ nguyên u77.

```bash
python3 tools/analyze_heater_step.py manual_step.csv \
  --svg manual_step.svg --json manual_step_model.json
```

Đồ thị gồm nhiệt độ đo thực, đường mô hình riêng và u thực theo thời gian;
đường đứng đánh dấu thời điểm đổi u. Nếu thiếu bước, có nhiều bước hoặc chưa
đủ mẫu, tool vẫn xuất đồ thị thực và JSON nêu lý do, trả mã **2**. Không gộp
lần dò nhiều mức thành một phép đo bước duy nhất.

Biểu đồ SVG hiển thị nhiệt độ đầu/cuối, min/max, tổng thời gian và số mẫu.
Trục thời gian có cả **phút:giây và giây**. Bảng bên phải ghi T1, nhiệt độ trung
bình đoạn cuối T2, thời điểm đổi u, thời gian trễ L, hằng số thời gian τ và độ
trôi nhiệt độ cuối nếu nhận dạng được. Nhãn **chưa xác lập** chỉ rõ khi nhiệt độ
cuối chưa thể coi là nhiệt độ cân bằng. Mốc 63,2% là giá trị theo mô hình;
nếu mốc nằm ngoài thời gian đo, biểu đồ ghi rõ và không vẽ thành điểm đo thực.

Mở SVG bằng Chrome/Edge/Firefox rồi rê chuột lên đường đo để xem chính xác
`t`, nhiệt độ và u ở từng mẫu. Ảnh PNG chỉ là bản xem tĩnh. Các tool dùng chung
`tools/thermal_chart.py`; khi chép tool sang máy khác phải chép kèm file này
(hoặc chép cả thư mục `tools`). `.bat` hiện mặc định dùng phân tích một u cố định.

Thư mục `data/charts/` chứa các biểu đồ mới dựng lại từ năm log cũ, gồm cả
đường nguội 0%; dữ liệu CSV, dump RAM và các JSON cũ giữ nguyên. Các thông số
hiển thị trên bản SVG mới được tính lại bằng công cụ hiện tại.

Với log mới, tool lấy trung bình đoạn cuối của pha u1 để xác định T1, khớp
đoạn sau bước u2, kiểm tra xác lập T1/T2 theo cửa sổ thời gian. Điều kiện
thêm để xuất PID mặc định là R² ≥0,90 và thời gian sau bước đủ L+3τ.
Có thể thay các ngưỡng thực nghiệm đã thống nhất:

```bash
python3 tools/analyze_heater_step.py manual_step.csv \
  --steady-window-s 300 --steady-band-c 1 --steady-slope-c-per-min 0.05 \
  --svg manual_step.svg --json manual_step_model.json
```

Mô hình làm việc theo **biến gia số quanh (u1,T1)**:

```text
ΔT(s) / Δu(s) = Ku × exp(-L s) / (τ s + 1)
Ku ≈ (T2 − T1) / (u2 − u1)       [°C / đơn vị u]
```

`step_time_s` là mốc đổi u; thời gian dùng cho đáp ứng là `t − step_time_s`.
JSON có `u1`, `u2`, `initial_temperature_c`, `measured_final_temperature_c`,
`measured_process_gain_c_per_u` (từ T1/T2 khi cả hai đạt xác lập),
`process_gain_c_per_u` (từ khớp đường cong), `dead_time_s`, `time_constant_s`
và các chỉ số dao động/độ dốc. `process_gain_c_per_fraction` dùng đầu vào
chuẩn hóa 0–1, bằng Ku ×255; phân biệt hai đơn vị khi dùng hàm truyền.

Đầu ra PID chỉ là ước lượng IMC ban đầu; các khóa `firmware_pid_*_0_to_255`
đã quy đổi sang thang 0–255. Khi T1/T2 chưa đạt, fit kém hoặc kết thúc do lỗi/
đầy RAM, mặc định tool không xuất PID. `--allow-unsettled-pid` chỉ phục vụ
nghiên cứu giá trị ngoại suy; không dùng thay cho phép đo xác lập.

## Dữ liệu cũ

Các thư mục `data/0_1`, `40_1`, `40_2`, `70_1`, `100_1` giữ nguyên số đo cũ.
Các tool phân tích nguội/nhiệt chung vẫn đọc được CSV cũ để đối chiếu, nhưng
chế độ tự gia nhiệt rồi nguội 0% và chọn cố định 40/70/100% đã được thay bằng
manual. Không xem số liệu cũ là phép đo u1 → u2 quanh vùng làm việc mới.
