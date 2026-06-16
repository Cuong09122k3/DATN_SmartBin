#include "sensor_manager.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "SENSOR";

// Khởi tạo các chân GPIO cảm biến hồng ngoại làm đầu vào có điện trở kéo lên
void sensor_manager_init(void)
{
    gpio_config_t io_conf = {};
 
    // Cấu hình mặt nạ bit cho 4 chân GPIO cảm biến hồng ngoại
    io_conf.pin_bit_mask = (1ULL << IR_INSERT_1_PIN) | // Cảm biến phát hiện rác 1
                           (1ULL << IR_INSERT_2_PIN) | // Cảm biến phát hiện rác 2
                           (1ULL << IR_FULL_1_PIN)   | // Cảm biến báo đầy Ngăn 1
                           (1ULL << IR_FULL_2_PIN);    // Cảm biến báo đầy Ngăn 2
 
    io_conf.mode         = GPIO_MODE_INPUT;          // Thiết lập chế độ Input đầu vào
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;       // Kích hoạt điện trở kéo lên nội bộ
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;     // Vô hiệu hóa điện trở kéo xuống
    io_conf.intr_type    = GPIO_INTR_DISABLE;        // Không sử dụng ngắt, sử dụng thăm dò (Polling)
 
    gpio_config(&io_conf);
 
    ESP_LOGI(TAG, "Sensor system ready.");
}

// Kiểm tra xem rác có được thả vào thùng hay không (sử dụng thuật toán Debounce chống nhiễu)
bool sensor_is_trash_inserted(void)
{
    // Đọc trạng thái cảm biến: Trả về 1 khi có rác cắt qua sóng hồng ngoại
    bool detected = (gpio_get_level((gpio_num_t)IR_INSERT_1_PIN) == 1 &&
                     gpio_get_level((gpio_num_t)IR_INSERT_2_PIN) == 1);
    
    if (!detected) return false;

    // Cơ chế Debounce: Trì hoãn 50ms và kiểm tra lại để loại bỏ nhiễu cơ học hoặc dao động sóng tạm thời
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Nếu sau 50ms sóng vẫn bị cắt, chứng tỏ rác đã thực sự được thả vào
    return (gpio_get_level((gpio_num_t)IR_INSERT_1_PIN) == 1 &&
            gpio_get_level((gpio_num_t)IR_INSERT_2_PIN) == 1);
}



// Kiểm tra trạng thái đầy của Ngăn 1 (Rác Pin/Nguy hại)
bool sensor_is_bin1_full(void)
{
    return (gpio_get_level((gpio_num_t)IR_FULL_1_PIN) == 0);
}

// Kiểm tra trạng thái đầy của Ngăn 2 (Rác Thường)
bool sensor_is_bin2_full(void)
{
    return (gpio_get_level((gpio_num_t)IR_FULL_2_PIN) == 0);
}
