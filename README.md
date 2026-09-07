# AS75 – nhận dạng nhiệt và tìm thông số PID

Firmware hiện chạy ở chế độ **thí nghiệm đáp ứng bước hở vòng**, thay cho chu
trình tiệt trùng tự động. Mục đích là ghi nhiệt độ theo thời gian tại ba mức
công suất để dựng biểu đồ, nhận dạng hàm truyền và lấy bộ thông số PID ban đầu.

Trong chế độ này **PID hoàn toàn không được gọi**. Công suất được chốt khi nhấn
START và không thay đổi theo nhiệt độ: 100% là 10 giây ON/0 giây OFF, 70% là
7 giây ON/3 giây OFF, và 40% là 4 giây ON/6 giây OFF. SSR chỉ có trạng thái
ON/OFF nên “công suất tuyến tính” ở đây là công suất trung bình theo thời gian,
không phải điện áp analog 40% hoặc 70%. Nhiệt độ chỉ được đọc để ghi dữ liệu và
ngắt bảo vệ; mẫu CSV dùng trực tiếp số đo PT100 chưa lọc để không đưa thêm độ
trễ giả vào hàm truyền.

## Cách chạy thí nghiệm

### Kết nối chỉ bằng ST-LINK (khuyến nghị)

Firmware xuất CSV qua **SWO/ITM stimulus port 0**, vì vậy chỉ cần đúng một dây
USB đang dùng để nạp/debug qua cổng ST-LINK trên STM32F407G-DISC1. Không cần
USB-to-UART và không cần cắm cổng USB OTG FS.

Trong STM32CubeIDE:

1. Nối cổng USB **ST-LINK** của board với PC và mở cấu hình `Debug`.
2. Trong `Debug Configurations > Debugger`, chọn giao tiếp `SWD`, bật
   `Serial Wire Viewer (SWV)` và đặt `Core Clock = 168000000 Hz`.
3. Bắt đầu phiên debug, rồi mở
   `Window > Show View > SWV > SWV ITM Data Console`.
4. Mở cấu hình ITM, bật **Stimulus Port 0**, nhấn `Start Trace`.
5. Lưu nội dung console thành file `.csv`, sau đó mới nhấn P1/P2/P3 và START
   trên board.

SWO không tạo COM port; dữ liệu xuất hiện trong `SWV ITM Data Console` của
STM32CubeIDE. Cần giữ phiên debug và `Start Trace` trong suốt phép đo. Nếu cấu
hình clock của project thay đổi, phải nhập lại đúng tần số core clock trong SWV.

### Thao tác đo

1. Xác nhận SWV ITM Data Console đang `Start Trace` trên stimulus port 0.
2. Chờ kiểm tra PT100, nước và cửa hoàn tất.
3. Nhấn **P1 = 100%**, **P2 = 70%**, hoặc **P3 = 40%**. Màn hình 1 hiện `H100`,
   `H 70`, hoặc `H 40`; màn hình 2 hiện nhiệt độ.
4. Bắt đầu lưu nội dung SWV ITM Data Console rồi nhấn **START**.
   Firmware điều chế SSR theo cửa sổ 10 giây, lấy mẫu mỗi 10 giây và tự dừng sau
   20 phút. Nhấn START lần nữa để dừng sớm. Mỗi lần chạy chỉ tạo khoảng 120 mẫu.
5. Để hệ thống nguội về cùng nhiệt độ ban đầu rồi mới chạy mức công suất kế
   tiếp. Nên lưu mỗi mức vào một file riêng (`step_100.csv`, `step_70.csv`,
   `step_40.csv`). Không vận hành thiết bị nếu các liên động an toàn chưa được
   kiểm chứng trên phần cứng thực.

Dòng dữ liệu có dạng:

```csv
elapsed_s,power_percent,temperature_c,status
10,70,27.4,RUN
```

CSV chỉ giữ bốn cột cần cho nhận dạng để file ngắn và dễ xử lý. Firmware vẫn
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
cho bộ điều khiển có miền đầu ra 0..255. Đây chỉ là điểm khởi đầu: kiểm tra ở công suất thấp,
giới hạn đầu ra và giữ bảo vệ quá nhiệt khi đưa PID trở lại máy.
