#ifndef CONFIG_H
#define CONFIG_H

// =============================================================
// CẤU HÌNH CHÂN GPIO (PINS)
// =============================================================

// --- Cảm biến hồng ngoại (Active-Low: LOW = có vật thể) ---
#define IR_INSERT_1_PIN 1  // Phát hiện rác vào #1
#define IR_INSERT_2_PIN 40 // Phát hiện rác vào #2
#define IR_FULL_1_PIN 38   // Báo đầy ngăn 1 (Rác nguy hại)
#define IR_FULL_2_PIN 39   // Báo đầy ngăn 2 (Rác thường)

// --- Động cơ Servo ---
#define SERVO_GPIO_NUM 14 // Chân PWM điều khiển nắp

// --- Module âm thanh JQ6500 (UART1) ---
#define JQ6500_TX_PIN 42
#define JQ6500_RX_PIN 41
#define JQ6500_BAUD 9600
#define AUDIO_DEFAULT_VOLUME 30 // 0-30

// --- Thông số góc Servo (microseconds) ---
#define SERVO_PULSE_MIN_US 500  // 0 độ
#define SERVO_PULSE_MID_US 1450 // 90 độ
#define SERVO_PULSE_MAX_US 2400 // 180 độ

// --- Góc quay của nắp lật ---
#define SERVO_ANGLE_HOME 90  // Đóng nắp
#define SERVO_ANGLE_BIN1 0   // Mở ngăn 1
#define SERVO_ANGLE_BIN2 180 // Mở ngăn 2

// --- Màn hình OLED SSD1306 (I2C) ---
#define OLED_SDA_GPIO 47
#define OLED_SCL_GPIO 21
#define OLED_I2C_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// --- Camera OV3660 ---
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5
#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

// =============================================================
// CẤU HÌNH WIFI
// =============================================================
#define WIFI_SSID "Cuong_SmartBin"
#define WIFI_PASSWORD "12345678"
#define WIFI_CONNECT_TIMEOUT_S 30 // Thời gian chờ kết nối WiFi tối đa (giây)

// =============================================================
// CẤU HÌNH CAMERA
// =============================================================
#define CAM_XCLK_FREQ_HZ 16500000
#define CAM_JPEG_QUALITY 4
#define CAM_FB_COUNT 3
#define CAPTURE_COLS 240
#define CAPTURE_ROWS 240
#define CAPTURE_FRAMESIZE FRAMESIZE_240X240

// Tùy chỉnh chất lượng ảnh
#define CAM_BRIGHTNESS -3
#define CAM_CONTRAST 1
#define CAM_SATURATION 2
#define CAM_SHARPNESS 2
#define CAM_HMIRROR 1
#define CAM_VFLIP 0

// =============================================================
// CẤU HÌNH THỜI GIAN (ms)
// =============================================================
#define TIMING_DETECTION_CONFIRM_MS 3000 // Chờ xác nhận rác vào
#define TIMING_CAPTURE_GAP_MS 50         // Nghỉ giữa các khung hình AI
#define TIMING_WAIT_DROP_MS 2000         // Chờ rác rơi
#define TIMING_COOLDOWN_MS 3000          // Nghỉ sau một chu kỳ
#define TIMING_DONE_WAIT_MS 500          // Chờ sau khi có kết quả
#define TIMING_FULL_POLL_MS 500          // Tần suất kiểm tra thùng đầy
#define TIMING_FULL_ALERT_MS 15000       // Nhắc lại thùng đầy
#define FULL_DEBOUNCE_COUNT 6            // Lần xác nhận thùng đầy liên tục
#define TIMING_STATE_TRANS_MS 1000       // Nghỉ khi chuyển trạng thái

// =============================================================
// CẤU HÌNH AI
// =============================================================
#define MODEL_INPUT_WIDTH 128
#define MODEL_INPUT_HEIGHT 128
#define CAPTURE_NUM_FRAMES 5       // Số khung hình để bầu chọn
#define AI_CONFIDENCE_THRESHOLD 70 // Ngưỡng tin cậy tối thiểu (%)
#define MAX_CLASSES 15             // Số nhãn tối đa
#define MAX_JPEG_SIZE (64 * 1024)  // Giới hạn file JPEG (64KB)

// =============================================================
// CẤU HÌNH ÂM THANH
// =============================================================
#define AUDIO_TRACK_READY 1
#define AUDIO_TRACK_DETECTED 2
#define AUDIO_TRACK_CLASSIFYING 3
#define AUDIO_TRACK_DONE 4
#define AUDIO_TRACK_NORMAL 5
#define AUDIO_TRACK_PIN 6
#define AUDIO_TRACK_BG 7
#define AUDIO_TRACK_FULL_NORM 8
#define AUDIO_TRACK_FULL_PIN 9
#define AUDIO_TRACK_HELLO 10
#define AUDIO_TRACK_BEEP 11

#endif // CONFIG_H
