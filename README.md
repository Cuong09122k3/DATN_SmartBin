# SMART BIN AI - HỆ THỐNG THÙNG RÁC PHÂN LOẠI THÔNG MINH SỬ DỤNG AI

**Đồ án tốt nghiệp** chuyên ngành *Kỹ thuật điện tử - Tin học Công nghiệp*.

Dự án thiết kế và triển khai hệ thống thùng rác tự động phân loại rác thải nguy hại (như pin) và rác thải sinh hoạt thông thường. Hệ thống tích hợp mô hình học máy dạng TinyML chạy suy luận trực tiếp trên vi điều khiển **ESP32-S3**, kết hợp giám sát và điều khiển qua giao diện Web Dashboard cục bộ.

---

## 1. Các Tính Năng Kỹ Thuật

*   **Trí tuệ nhân tạo biên (Edge AI)**: Sử dụng thư viện **TensorFlow Lite Micro** để chạy mô hình mạng nơ-ron tích chập (CNN) trực tiếp trên ESP32-S3. Quá trình phân loại không phụ thuộc vào kết nối internet hay máy chủ ngoại vi.
*   **Thuật toán biểu quyết (Voting)**: Khi cảm biến phát hiện vật thể, hệ thống chụp liên tiếp **5 khung hình** (khoảng cách 50ms giữa các khung hình). Bộ phân loại áp dụng ngưỡng tin cậy (threshold) tối thiểu **70%** và thực hiện bỏ phiếu số đông nhằm giảm thiểu sai số do nhiễu động hoặc góc chụp.
*   **Kiến trúc hệ thống song song (FSM & FreeRTOS)**: Chương trình điều khiển dựa trên máy trạng thái hữu hạn (FSM) chạy trên một tác vụ độc lập (Core 1), tách biệt với tác vụ quản lý kết nối mạng và Web Server (Core 0), đảm bảo đáp ứng thời gian thực cho các cơ cấu cơ khí.
*   **Quản lý kết nối Wi-Fi tự động**: Tự động chuyển đổi giữa chế độ Wi-Fi Station (kết nối mạng nội bộ) và SoftAP (phát điểm truy cập tên `SmartBin_WiFi` để người dùng cấu hình thông số mạng khi không kết nối được Wi-Fi trạm).
*   **Web Dashboard giám sát**:
    *   Hiển thị trạng thái đầy/vơi của từng ngăn chứa rác dựa trên cảm biến hồng ngoại.
    *   Hiển thị kết quả nhận diện của AI, bao gồm phân phối xác suất và số phiếu bầu.
    *   Cho phép xem lại các ảnh JPEG thực tế được chụp và phân loại.
    *   Tích hợp chức năng chụp ảnh và kích hoạt mở nắp thủ công qua HTTP API.
    *   Cung cấp giao diện quét và cấu hình mạng Wi-Fi cục bộ.
*   **Tương tác âm thanh và màn hình**: Module âm thanh **JQ6500** giao tiếp qua UART phát thông báo tiếng Việt, kết hợp màn hình **OLED SSD1306** hiển thị trạng thái hoạt động và địa chỉ IP.

---

## 2. Sơ Đồ Đấu Nối Ngoại Vi (Pinout)

Hệ thống được thiết kế và vận hành trên kit phát triển **ESP32-S3 (phiên bản N16R8)** tích hợp 16MB Flash và 8MB PSRAM:

| Ngoại vi | Chân GPIO trên ESP32-S3 | Chú thích |
| :--- | :--- | :--- |
| **OLED SDA** | **GPIO 47** | Giao tiếp I2C hiển thị thông tin |
| **OLED SCL** | **GPIO 21** | Giao tiếp I2C hiển thị thông tin |
| **Servo PWM** | **GPIO 14** | Tín hiệu PWM điều khiển nắp lật phân loại |
| **UART1 TX (Audio)** | **GPIO 41** | Kết nối tới chân RX của module JQ6500 |
| **UART1 RX (Audio)** | **GPIO 42** | Kết nối tới chân TX của module JQ6500 |
| **IR Insert 1** | **GPIO 1** | Cảm biến phát hiện rác vào khay #1 (Active-Low) |
| **IR Insert 2** | **GPIO 40** | Cảm biến phát hiện rác vào khay #2 (Active-Low) |
| **IR Full 1** | **GPIO 38** | Cảm biến báo đầy ngăn 1 (Active-Low) |
| **IR Full 2** | **GPIO 39** | Cảm biến báo đầy ngăn 2 (Active-Low) |
| **Camera DCMI** | **GPIO 11, 9, 8, 10, 12, 18, 17, 16** | Đường truyền dữ liệu hình ảnh 8-bit DVP |
| **Camera Control**| **GPIO 15, 6, 7, 13, 4, 5** | XCLK, VSYNC, HREF, PCLK, SIOD (SDA), SIOC (SCL) |

*Lưu ý:* Hệ thống yêu cầu nguồn cấp ổn định **5V-2A** để tránh sụt áp gây khởi động lại vi điều khiển khi động cơ Servo MG996R hoạt động cùng lúc với camera và module âm thanh.

---

## 3. Cấu Trúc Mã Nguồn

Mã nguồn C++ được tổ chức dạng mô-đun trong thư mục [main/](./main):
*   `config.h`: Khai báo cấu hình GPIO, thông số Wi-Fi mặc định, tham số camera, các khoảng trễ thời gian và ID file âm thanh của module JQ6500.
*   `main.cpp`: Điểm khởi chạy hệ thống, khởi tạo phần cứng và quản lý tác vụ nền.
*   `system_fsm.cpp`: Triển khai máy trạng thái điều khiển vòng đời hoạt động của thiết bị (chờ rác -> chụp ảnh/suy luận -> điều khiển servo/phát âm thanh).
*   `inference.cpp`: Tiền xử lý ảnh (nội suy song tuyến và lượng tử hóa sang INT8) và gọi bộ thông dịch TensorFlow Lite Micro.
*   `model_data.h`: Chứa mảng byte phẳng của mô hình nhận diện 3 lớp: `"Background"`, `"Battery_S1"`, và `"Normal_S2"`.
*   `camera_manager.cpp`: Driver điều khiển camera OV3660, quản lý bộ đệm DMA và đồng bộ hóa bằng Mutex.
*   `servo_controller.cpp`: Điều khiển góc quay nắp lật phân loại sử dụng bộ tạo xung PWM (LEDC) của ESP32-S3.
*   `display_manager.cpp` & `sensor_manager.cpp`: Điều khiển màn hình OLED SSD1306 và lọc nhiễu tín hiệu cảm biến hồng ngoại.
*   `audio_player.cpp`: Gửi tập lệnh HEX qua UART điều khiển module JQ6500 phát âm thanh hướng dẫn.
*   `wifi_manager.cpp` & `web_server.cpp`: Quản lý kết nối mạng Wi-Fi và HTTP Server cung cấp giao diện điều khiển cùng API.
*   `web_data/`: Chứa mã nguồn giao diện web tĩnh (HTML, CSS, JS, hình ảnh).

---

## 4. Quản Lý Mô Hình AI & Dataset

### 4.1. Mô hình tích hợp sẵn
Mô hình AI nhận diện rác đã được huấn luyện và nhúng tĩnh dưới dạng mảng byte phẳng trong file [main/model_data.h](./main/model_data.h). Người dùng có thể biên dịch và chạy trực tiếp dự án mà không cần qua bước huấn luyện lại.

### 4.2. Dataset và quy trình huấn luyện lại
Thư mục dữ liệu ảnh gốc `dataset/` được cấu hình loại trừ trong `.gitignore` để tối ưu dung lượng mã nguồn.
*   **Liên kết tải bộ Dataset**: [Tải về từ Google Drive](https://drive.google.com/drive/folders/1lRUXjSxhUY2b_Wt7WxumkmFoFeFpGd6I?usp=drive_link)

Để thực hiện huấn luyện lại mô hình, làm theo các bước sau:

#### **Bước 1: Thiết lập thư mục dữ liệu**
Tải bộ dữ liệu từ liên kết trên và giải nén vào thư mục gốc của dự án với cấu trúc:
```text
smart_bin_idf_final/
├── dataset/
│   ├── train/
│   │   ├── Background/         # Ảnh khay lật rỗng
│   │   ├── Battery_S1/         # Ảnh pin và rác nguy hại (Ngăn 1)
│   │   └── Normal_S2/          # Ảnh rác thải thông thường (Ngăn 2)
│   └── test/
│       ├── Background/
│       ├── Battery_S1/
│       └── Normal_S2/
```

#### **Bước 2: Cài đặt và thực thi script huấn luyện**
Chạy các lệnh sau từ thư mục gốc của dự án để chuẩn bị môi trường và huấn luyện mô hình:
```bash
# Khởi tạo môi trường ảo Python
python -m venv .venv

# Kích hoạt môi trường ảo
# Trên Windows:
.venv\Scripts\activate
# Trên Linux/macOS:
source .venv/bin/activate

# Cài đặt thư viện phụ thuộc
pip install --upgrade pip
pip install tensorflow matplotlib numpy tensorflow-model-optimization scikit-learn tf-keras seaborn pandas

# Chuyển vào thư mục scripts và chạy quy trình huấn luyện
cd scripts
python train.py            # Huấn luyện mô hình Keras (xuất model.keras)
python evaluate_model.py   # Đánh giá mô hình (xuất báo cáo tại thư mục evaluation/)
python convert_model.py    # Lượng tử hóa sang TFLite INT8 (xuất model_quantized.tflite)
python verify_tflite.py    # So sánh sai số giữa Keras và TFLite
python export_model.py     # Chuyển đổi và cập nhật trực tiếp vào main/model_data.h
```

---

## 5. Biên Dịch & Nạp Chương Trình (Build & Flash)

Dự án được xây dựng và kiểm thử thành công trên môi trường **ESP-IDF v5.4.2**.

### Bước 1: Chuẩn bị môi trường
Cài đặt và thiết lập đường dẫn môi trường cho công cụ dòng lệnh ESP-IDF (IDF Command Prompt hoặc Shell).

### Bước 2: Biên dịch dự án
Mọi cấu hình phần cứng mặc định (PSRAM, phân vùng Flash, bộ nhớ đệm) đã được định nghĩa trong file `sdkconfig.defaults`. Biên dịch dự án bằng lệnh:
```bash
idf.py build
```

### Bước 3: Nạp chương trình và theo dõi cổng serial
Kết nối kit ESP32-S3 với máy tính và chạy lệnh:
```bash
idf.py flash monitor
```
*Lưu ý:* Phân vùng SPIFFS chứa giao diện Web trong thư mục `main/web_data/` sẽ tự động được đóng gói và nạp vào phân vùng `storage` của Flash khi thực hiện lệnh trên.

---

## 6. Hướng Dẫn Sử Dụng Dashboard
1.  Sau khi khởi động, màn hình OLED hiển thị trạng thái kết nối Wi-Fi.
2.  Nếu kết nối Wi-Fi trạm thành công, OLED hiển thị địa chỉ IP được cấp (ví dụ: `192.168.1.15`).
3.  Nếu không tìm thấy Wi-Fi trạm, thiết bị tự phát AP `SmartBin_WiFi` (mật khẩu: `12345678`), IP mặc định là `192.168.4.1`.
4.  Mở trình duyệt web trên thiết bị cùng mạng và truy cập địa chỉ IP hiển thị trên OLED để vào giao diện quản lý.


