#include "camera_manager.h"
#include "config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include <string.h>

static const char* TAG = "CAM";
SemaphoreHandle_t g_camera_mutex = NULL;

// Khởi tạo và cấu hình Camera OV3660 sử dụng chuẩn truyền thông SCCB và I2S DMA
esp_err_t camera_manager_init(void)
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; // Kênh LEDC phát xung clock XCLK cho camera
    config.ledc_timer   = LEDC_TIMER_0;
    
    // Gán các chân GPIO kết nối vật lý với camera OV3660 (ESP32-S3 Cam)
    config.pin_d0       = (gpio_num_t)Y2_GPIO_NUM;
    config.pin_d1       = (gpio_num_t)Y3_GPIO_NUM;
    config.pin_d2       = (gpio_num_t)Y4_GPIO_NUM;
    config.pin_d3       = (gpio_num_t)Y5_GPIO_NUM;
    config.pin_d4       = (gpio_num_t)Y6_GPIO_NUM;
    config.pin_d5       = (gpio_num_t)Y7_GPIO_NUM;
    config.pin_d6       = (gpio_num_t)Y8_GPIO_NUM;
    config.pin_d7       = (gpio_num_t)Y9_GPIO_NUM;
    config.pin_xclk     = (gpio_num_t)XCLK_GPIO_NUM;
    config.pin_pclk     = (gpio_num_t)PCLK_GPIO_NUM;
    config.pin_vsync    = (gpio_num_t)VSYNC_GPIO_NUM;
    config.pin_href     = (gpio_num_t)HREF_GPIO_NUM;
    config.pin_sccb_sda = (gpio_num_t)SIOD_GPIO_NUM;
    config.pin_sccb_scl = (gpio_num_t)SIOC_GPIO_NUM;
    config.pin_pwdn     = (gpio_num_t)PWDN_GPIO_NUM;
    config.pin_reset    = (gpio_num_t)RESET_GPIO_NUM;
    
    config.xclk_freq_hz = CAM_XCLK_FREQ_HZ;                // Tần số XCLK phát cho Camera (20MHz)
    config.pixel_format = PIXFORMAT_JPEG;                  // Định dạng ảnh đầu ra nén JPEG
    config.frame_size   = (framesize_t)CAPTURE_FRAMESIZE;  // Độ phân giải hình ảnh
    config.jpeg_quality = CAM_JPEG_QUALITY;                // Chất lượng ảnh JPEG (độ nén: 0-63, số nhỏ chất lượng cao)
    config.fb_count     = CAM_FB_COUNT;                    // Số lượng bộ đệm khung hình (Frame Buffers) để tránh trễ ảnh
    config.fb_location  = CAMERA_FB_IN_PSRAM;              // Cấp phát bộ đệm khung hình trên bộ nhớ RAM ngoài PSRAM
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;          // Chỉ chụp khi bộ đệm DMA rỗng
 
    // Khởi tạo thư viện driver Camera ESP32
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao Camera: 0x%x", err);
        return err;
    }
 
    // Tự động tinh chỉnh các thông số chất lượng hình ảnh đối với cảm biến OV3660
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL && s->id.PID == OV3660_PID) {
        s->set_quality(s,    CAM_JPEG_QUALITY);
        s->set_brightness(s, CAM_BRIGHTNESS);
        s->set_contrast(s,   CAM_CONTRAST);
        s->set_saturation(s, CAM_SATURATION);
        s->set_sharpness(s,  CAM_SHARPNESS);
        s->set_awb_gain(s, 1); // Bật tính năng cân bằng trắng tự động
        s->set_aec2(s, 1);     // Bật tự động kiểm soát độ phơi sáng
        s->set_lenc(s, 1);     // Kích hoạt tính năng sửa méo góc ống kính
        s->set_hmirror(s, CAM_HMIRROR);
        s->set_vflip(s,   CAM_VFLIP);
    }
 
    // Tạo Mutex bảo vệ tài nguyên camera chống xung đột luồng
    g_camera_mutex = xSemaphoreCreateMutex();
 
    ESP_LOGI(TAG, "Camera OV3660 da san sang.");
    return ESP_OK;
}
 
// Giải phóng (xả) các khung hình cũ bị lưu đọng trong bộ đệm DMA
// Điều này cực kỳ quan trọng để đảm bảo ảnh chụp ra luôn là ảnh thời gian thực mới nhất, không bị lấy ảnh cũ
void camera_flush_buffer(int count)
{
    int flush_count = (count <= 0) ? CAM_FB_COUNT : count;    // Số lượng ảnh cần xả
    
    if (xSemaphoreTake(g_camera_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        for (int i = 0; i < flush_count; i++) {
            camera_fb_t *fb = esp_camera_fb_get(); // Lấy ảnh từ bộ đệm
            if (fb) {
                esp_camera_fb_return(fb); // Trả lại bộ đệm cho driver mà không xử lý ảnh
            }
        }
        xSemaphoreGive(g_camera_mutex);
    }
}
 
// Chụp ảnh phân loại AI 
bool camera_capture_to_buffers(int idx, uint8_t* rgb_out, uint8_t* jpeg_out, size_t* jpeg_len_out)
{
    bool res = false;
    // Chiếm quyền truy cập Camera với thời gian chờ tối đa 5 giây
    if (xSemaphoreTake(g_camera_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        camera_fb_t *fb = esp_camera_fb_get(); // Lấy ảnh từ bộ đệm
        if (!fb) {
            ESP_LOGE(TAG, "Loi: Khong the lay du lieu hinh anh!");
            xSemaphoreGive(g_camera_mutex);
            return false;
        }
 
        // Nếu có bộ đệm lưu ảnh JPEG, tiến hành sao chép dữ liệu để chuẩn bị truyền lên Web
        if (jpeg_out != NULL) {
            if (fb->len <= MAX_JPEG_SIZE) {
                memcpy(jpeg_out, fb->buf, fb->len);
                if (jpeg_len_out != NULL) *jpeg_len_out = fb->len;
            } else {
                if (jpeg_len_out != NULL) *jpeg_len_out = 0;
            }
        }
 
        // Chuyển đổi ảnh JPEG sang định dạng màu RGB888 phục vụ trực tiếp cho mô hình suy luận TinyML
        res = fmt2rgb888(fb->buf, fb->len, fb->format, rgb_out);
        
        esp_camera_fb_return(fb); // Giải phóng bộ đệm khung hình sau khi chụp
        xSemaphoreGive(g_camera_mutex);
    }
    return res;
}
 
// Chụp ảnh thủ công tức thời theo lệnh từ Web Dashboard (Thread-safe)
void camera_capture_manual(uint8_t* jpeg_out, size_t* jpeg_len_out)
{
    if (xSemaphoreTake(g_camera_mutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        // Xả sạch toàn bộ ảnh cũ tồn đọng trong bộ đệm DMA
        for (int i = 0; i < CAM_FB_COUNT; i++) {
            camera_fb_t *tmp_fb = esp_camera_fb_get();
            if (tmp_fb) esp_camera_fb_return(tmp_fb);
        }
 
        // Lấy khung hình mới nhất sau khi đã xả đệm
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            if (fb->len <= MAX_JPEG_SIZE && jpeg_out != NULL) {
                memcpy(jpeg_out, fb->buf, fb->len);
                if (jpeg_len_out != NULL) *jpeg_len_out = fb->len;
            }
            esp_camera_fb_return(fb);
        }
        xSemaphoreGive(g_camera_mutex);
    }
}
