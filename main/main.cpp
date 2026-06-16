#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "audio_player.h"
#include "camera_manager.h"
#include "config.h"
#include "display_manager.h"
#include "inference.h"
#include "sensor_manager.h"
#include "servo_controller.h"
#include "system_fsm.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "MAIN";

// Điểm khởi chạy ứng dụng chính (Application Entry Point)
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Khoi dong Smart Bin AI...");

  // 1. Khởi tạo màn hình OLED SSD1306 qua giao tiếp I2C
  if (display_init()) {
    display_show_status("KHOI DONG...");
  }

  // 2. Cấp phát động các vùng nhớ đệm RGB và JPEG trên PSRAM ngoài để tránh cạn kiệt RAM nội bộ
  if (alloc_image_buffers() != ESP_OK) {
    ESP_LOGE(TAG, "Loi cap phat bo nho PSRAM!");
    display_show_status("ERR: PSRAM");
    return;
  }

  // 3. Thiết lập các chân ngoại vi GPIO (Cảm biến hồng ngoại phát hiện rác/đầy và Servo điều khiển nắp lật)
  sensor_manager_init();
  servo_controller_init();
  
  // 4. Khởi tạo cổng truyền thông UART kết nối với mô-đun phát âm thanh JQ6500
  audio_player_init();

  // 5. Khởi động cảm biến ảnh OV3660 qua giao tiếp DVP
  if (camera_manager_init() != ESP_OK) {
    display_show_status("ERR: CAMERA");
    return;
  }

  // 6. Tải mô-đun TinyML, khởi tạo Tensor Arena trên PSRAM và thiết lập TensorFlow Lite Interpreter
  if (!inference_init()) {
    display_show_status("ERR: AI");
    return;
  }

  // 7. Khởi chạy ngăn xếp TCP/IP, cấu hình WiFi (Station/APSTA) và Web Server điều khiển/giám sát từ xa
  wifi_manager_init();
  web_server_init();

  // Phát nhạc chào mừng hệ thống khởi động hoàn tất
  audio_play_track(AUDIO_TRACK_HELLO);
  
  // 8. Bắt đầu luồng máy trạng thái FSM (FreeRTOS Task chạy độc lập trên Core 1)
  system_fsm_start();

  ESP_LOGI(TAG, "Khoi dong hoan tat.");

  // Vòng lặp giám sát địa chỉ IP: Định kỳ kiểm tra IP và hiển thị lên màn hình OLED khi có thay đổi (WiFi kết nối hoặc tự phát AP)
  char last_ip[16] = "0.0.0.0";
  while (1) {
    char ip_str[16];
    wifi_manager_get_ip(ip_str);
    if (strcmp(ip_str, last_ip) != 0) {
      display_show_network(ip_str);
      strcpy(last_ip, ip_str);
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Nghỉ 1 giây giữa các lượt quét
  }
}

