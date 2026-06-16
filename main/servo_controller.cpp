/**
 * servo_controller.cpp - Triển khai điều khiển Servo bằng LEDC PWM (ESP-IDF Port).
 *
 * Thay thế thư viện ESP32Servo (Arduino) bằng ngoại vi LEDC native của ESP-IDF.
 *
 * Nguyên lý:
 *  - LEDC Timer: 50Hz, độ phân giải 16-bit → Period = 20ms = 65536 ticks
 *  - Pulse 500µs  = 0 độ   → duty = (500 /20000) * 65536 ≈ 1638
 *  - Pulse 1450µs = 90 độ  → duty = (1450/20000) * 65536 ≈ 4751
 *  - Pulse 2400µs = 180 độ → duty = (2400/20000) * 65536 ≈ 7864
 */
#include "servo_controller.h"
#include "config.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char* TAG = "SERVO";

// --- Cấu hình LEDC ---
#define SERVO_LEDC_TIMER      LEDC_TIMER_1
#define SERVO_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_CHANNEL    LEDC_CHANNEL_1
#define SERVO_LEDC_RESOLUTION LEDC_TIMER_13_BIT    // 8192 bước (ESP32-S3 hỗ trợ tối đa 14-bit)
#define SERVO_LEDC_FREQ_HZ    50                   // 50Hz = chu kỳ 20ms (chuẩn Servo)

// --- Biên giới xung (microseconds) ---
#define SERVO_PERIOD_US       20000  // Chu kỳ 20ms = 20000µs

// Hàm chuyển đổi độ dài xung (microsecond) sang giá trị điều khiển của LEDC
static uint32_t us_to_duty(uint32_t pulse_us)
{
    return (uint32_t)((uint64_t)pulse_us * 8191ULL / SERVO_PERIOD_US);
}

// Hàm chuyển đổi góc quay (0-180 độ) sang giá trị điều khiển tương ứng
static uint32_t angle_to_duty(int angle_deg)
{
    if (angle_deg < 0)   angle_deg = 0;
    if (angle_deg > 180) angle_deg = 180;

    // Tính toán độ dài xung dựa trên góc quay
    uint32_t pulse_us = SERVO_PULSE_MIN_US +
                        (uint32_t)((SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) * angle_deg / 180);
    return us_to_duty(pulse_us);
}

void servo_controller_init(void)
{
    // 1. Cấu hình bộ định thời (Timer) cho tín hiệu PWM
    // Tần số 50Hz là tiêu chuẩn để điều khiển động cơ Servo
    ledc_timer_config_t timer_conf = {
        .speed_mode      = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_RESOLUTION,
        .timer_num       = SERVO_LEDC_TIMER,
        .freq_hz         = SERVO_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
        .deconfigure     = false
    };
    ledc_timer_config(&timer_conf);

    // 2. Cấu hình kênh điều khiển (Channel) và chân GPIO
    ledc_channel_config_t ch_conf = {
        .gpio_num   = SERVO_GPIO_NUM,
        .speed_mode = SERVO_LEDC_MODE,
        .channel    = SERVO_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = SERVO_LEDC_TIMER,
        .duty       = angle_to_duty(SERVO_ANGLE_HOME),  // Mặc định nắp ở vị trí đóng
        .hpoint     = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags      = { .output_invert = 0 }
    };
    ledc_channel_config(&ch_conf);

    ESP_LOGI(TAG, "Dong co Servo da san sang (GPIO %d).", SERVO_GPIO_NUM);
}

// Hàm điều khiển Servo quay đến một góc xác định
void servo_set_angle(int angle_deg)
{
    uint32_t duty = angle_to_duty(angle_deg);
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty);
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL); // Cập nhật để áp dụng thay đổi
}

// Mở ngăn 1 (Rác nguy hại/Pin)
void servo_open_bin1(void)
{
    servo_set_angle(SERVO_ANGLE_BIN1);
}

// Mở ngăn 2 (Rác thường)
void servo_open_bin2(void)
{
    servo_set_angle(SERVO_ANGLE_BIN2);
}

// Đóng nắp về vị trí ban đầu
void servo_home(void)
{
    servo_set_angle(SERVO_ANGLE_HOME);
}
