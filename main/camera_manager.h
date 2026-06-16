#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include "esp_camera.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SemaphoreHandle_t g_camera_mutex; // Mutex đồng bộ hóa truy cập tài nguyên camera tránh tranh chấp luồng

// Khởi tạo cấu hình và tham số cho Camera OV3660
esp_err_t camera_manager_init(void);

// Hàm xả (flush) sạch các khung hình cũ còn đọng lại trong bộ đệm DMA
void camera_flush_buffer(int count);

// Chụp ảnh phân loại AI, lưu trữ đồng thời ảnh RGB888 (suy luận) và ảnh JPEG (gửi Web)
bool camera_capture_to_buffers(int idx, uint8_t* rgb_out, uint8_t* jpeg_out, size_t* jpeg_len_out);

// Chụp ảnh thủ công theo yêu cầu từ giao diện Web Dashboard
void camera_capture_manual(uint8_t* jpeg_out, size_t* jpeg_len_out);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_MANAGER_H
