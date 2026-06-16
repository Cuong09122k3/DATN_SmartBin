#include "display_manager.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ssd1306.h"

static const char *TAG = "OLED";
static ssd1306_handle_t g_ssd1306_handle = NULL; // Con trỏ tới màn hình
static i2c_master_bus_handle_t g_i2c_bus_handle = NULL;
static SemaphoreHandle_t g_display_mutex = NULL;
static char g_last_ip[64] = "0.0.0.0";

// Khởi tạo kênh truyền thông I2C Master kết nối và điều khiển OLED SSD1306
bool display_init(void) {
  ESP_LOGI(TAG, "Initializing SSD1306 (SDA:%d, SCL:%d)", OLED_SDA_GPIO, OLED_SCL_GPIO);

  // 1. Cấu hình các thông số cho I2C Master Bus
  i2c_master_bus_config_t i2c_bus_config = {};
  i2c_bus_config.i2c_port = I2C_NUM_0;
  i2c_bus_config.sda_io_num = (gpio_num_t)OLED_SDA_GPIO;
  i2c_bus_config.scl_io_num = (gpio_num_t)OLED_SCL_GPIO;
  i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  i2c_bus_config.glitch_ignore_cnt = 7;
  i2c_bus_config.flags.enable_internal_pullup = 1; // Kích hoạt điện trở kéo lên nội bộ cho đường truyền I2C

  // Tạo và thiết lập master bus I2C mới
  esp_err_t err = i2c_new_master_bus(&i2c_bus_config, &g_i2c_bus_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C error: %s", esp_err_to_name(err));
    return false;
  }

  // 2. Cấu hình driver SSD1306
  ssd1306_config_t dev_config = {};
  dev_config.i2c_address = OLED_I2C_ADDR;               // Địa chỉ I2C của màn hình (thường là 0x3C)
  dev_config.i2c_clock_speed = 100000;                  // Tần số xung nhịp I2C (100kHz)
  dev_config.panel_size = SSD1306_PANEL_128x64;          // Kích thước màn hình OLED 128x64
  dev_config.display_enabled = true;

  // Khởi chạy màn hình OLED qua giao tiếp I2C Bus vừa tạo
  err = ssd1306_init(g_i2c_bus_handle, &dev_config, &g_ssd1306_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SSD1306 error: %s", esp_err_to_name(err));
    return false;
  }

  // Khởi tạo Mutex bảo vệ luồng ghi ký tự SSD1306 tránh lỗi tranh chấp I2C
  g_display_mutex = xSemaphoreCreateMutex();
  ssd1306_clear_display(g_ssd1306_handle, false);       // Xóa sạch bộ đệm hiển thị
  ssd1306_set_contrast(g_ssd1306_handle, 0xFF);         // Cấu hình độ tương phản sáng tối đa

  ESP_LOGI(TAG, "SSD1306 ready.");
  return true;
}

// Hiển thị trạng thái của hệ thống lên OLED 
void display_show_status(const char *status) {
  if (!g_ssd1306_handle || !g_display_mutex) return; 
  
  // Chiếm quyền truy cập OLED bằng Mutex
  if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    // Xóa sạch các dòng thông tin cũ để chuẩn bị ghi đè dữ liệu mới
    ssd1306_display_text(g_ssd1306_handle, 1, "                ", false);
    ssd1306_display_text(g_ssd1306_handle, 3, "                ", false);
    
    // Ghi trạng thái hoạt động lên dòng 1
    ssd1306_display_text(g_ssd1306_handle, 1, (char *)status, false);

    // Ghi đè địa chỉ IP hiện tại ở góc dưới cùng màn hình (dòng 7)
    char ip_buf[32];
    snprintf(ip_buf, sizeof(ip_buf), "%-16.16s", g_last_ip);
    ssd1306_display_text(g_ssd1306_handle, 7, ip_buf, false);

    xSemaphoreGive(g_display_mutex); // Giải phóng Mutex
  }
}

// Hiển thị kết quả trúng cử TinyML và độ tự tin lên OLED (Thread-safe)
void display_show_result(const char *label, float confidence) {
  if (!g_ssd1306_handle || !g_display_mutex) return;
  if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    // Xóa toàn bộ các dòng phía trên dòng IP
    for (int i = 0; i <= 6; i++) {
      ssd1306_display_text(g_ssd1306_handle, i, "                ", false);
    }
    // Hiển thị tên nhãn rác chiến thắng ở dòng 1
    ssd1306_display_text(g_ssd1306_handle, 1, (char *)label, false);

    // Hiển thị phần trăm độ tin cậy ở dòng 3
    char buf[32];
    snprintf(buf, sizeof(buf), "Tin cay: %.2f%%", confidence);
    ssd1306_display_text(g_ssd1306_handle, 3, buf, false);

    // Giữ nguyên dòng hiển thị địa chỉ IP ở dưới cùng (dòng 7)
    char ip_buf[32];
    snprintf(ip_buf, sizeof(ip_buf), "%-16.16s", g_last_ip);
    ssd1306_display_text(g_ssd1306_handle, 7, ip_buf, false);

    xSemaphoreGive(g_display_mutex);
  }
}



// Hiển thị địa chỉ IP mạng nội bộ của thiết bị lên dòng cuối màn hình OLED (Thread-safe)
void display_show_network(const char *ip) {
  if (!g_ssd1306_handle || !g_display_mutex) return;
  strncpy(g_last_ip, ip, sizeof(g_last_ip) - 1);
  if (xSemaphoreTake(g_display_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%-16.16s", ip);
    ssd1306_display_text(g_ssd1306_handle, 7, buf, false);
    xSemaphoreGive(g_display_mutex);
  }
}
