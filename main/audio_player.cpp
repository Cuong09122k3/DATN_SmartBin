#include "audio_player.h"
#include "config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "AUDIO";
static const uart_port_t UART_NUM = UART_NUM_1;        // Sử dụng cổng UART 1 để truyền thông
static SemaphoreHandle_t s_audio_mutex = NULL;          // Mutex bảo vệ luồng gửi tín hiệu tránh nghẽn UART
static int64_t s_last_cmd_time = 0;                     // Lưu mốc thời gian gửi lệnh gần nhất để khống chế khoảng trễ tối thiểu

// Khởi tạo UART 1 kết nối với module âm thanh JQ6500
void audio_player_init(void) {
    // Tạo Mutex đồng bộ hóa truy cập module âm thanh
    if (s_audio_mutex == NULL) {
        s_audio_mutex = xSemaphoreCreateMutex();
    }

    // Cấu hình các thông số UART tiêu chuẩn của module JQ6500 (Baudrate: 9600)
    uart_config_t uart_config = {};
    uart_config.baud_rate = JQ6500_BAUD;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;
    
    // Cài đặt UART driver với bộ đệm truyền 256 bytes
    uart_driver_install(UART_NUM, 256, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, JQ6500_TX_PIN, JQ6500_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    vTaskDelay(pdMS_TO_TICKS(100)); // Chờ ngoại vi ổn định điện áp

    // Thiết lập âm lượng mặc định ban đầu cho loa qua tập lệnh HEX: {0x7E, 0x03, 0x06, Volume, 0xEF}
    uint8_t vol_cmd[] = {0x7E, 0x03, 0x06, (uint8_t)AUDIO_DEFAULT_VOLUME, 0xEF};
    uart_write_bytes(UART_NUM, (const char*)vol_cmd, sizeof(vol_cmd));

    ESP_LOGI(TAG, "Audio module ready.");
}

// Gửi lệnh UART phát âm thanh theo số hiệu file (Thread-safe thông qua s_audio_mutex)
void audio_play_track(uint16_t track_id) {
    if (s_audio_mutex == NULL) return;

    // Chiếm quyền sử dụng Mutex để đảm bảo chỉ có duy nhất 1 luồng được ghi vào UART tại một thời điểm
    if (xSemaphoreTake(s_audio_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        int64_t now = esp_timer_get_time() / 1000;
        int64_t diff = now - s_last_cmd_time;
        
        // Cơ chế Debounce: Mỗi lệnh gửi đi phải cách nhau tối thiểu 300ms để module JQ6500 kịp phản hồi và tránh treo chip
        if (diff < 300) {
            vTaskDelay(pdMS_TO_TICKS(300 - diff));
        }

        // Gửi khung lệnh HEX phát file: {0x7E, 0x04, 0x03, HighByte_TrackID, LowByte_TrackID, 0xEF}
        uint8_t cmd[] = {0x7E, 0x04, 0x03, (uint8_t)(track_id >> 8), (uint8_t)(track_id & 0xFF), 0xEF};
        uart_write_bytes(UART_NUM, (const char*)cmd, sizeof(cmd));
        
        s_last_cmd_time = esp_timer_get_time() / 1000; // Cập nhật mốc thời gian gửi lệnh
        xSemaphoreGive(s_audio_mutex);                 // Giải phóng quyền Mutex
        
        ESP_LOGI(TAG, "Playing Track: %d", track_id);
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Chờ 50ms giãn cách
    }
}
