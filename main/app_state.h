#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// ===============================================================
// BỘ ĐỆM HÌNH ẢNH (PSRAM) - Cấp phát trên bộ nhớ ngoài để tránh tràn RAM nội bộ
// ===============================================================
extern uint8_t *rgb_buf;                             // Bộ đệm chứa dữ liệu ảnh RGB888 thô phục vụ suy luận TinyML
extern uint8_t *ai_jpeg_buffers[CAPTURE_NUM_FRAMES]; // Mảng bộ đệm lưu trữ 5 ảnh JPEG gần nhất để truyền lên Web
extern size_t   ai_jpeg_lens[CAPTURE_NUM_FRAMES];    // Độ dài thực tế (bytes) của từng file ảnh JPEG AI
extern uint8_t *manual_jpeg_buf;                     // Bộ đệm chứa ảnh chụp thủ công theo lệnh từ Web
extern size_t   manual_jpeg_len;                     // Độ dài thực tế (bytes) của ảnh chụp thủ công

// ===============================================================
// KẾT QUẢ DỰ ĐOÁN & ĐỒNG BỘ HÓA
// ===============================================================
extern char g_prediction_str[128];                   // Chuỗi kết quả dự đoán hiển thị trên OLED và Web Dashboard
extern SemaphoreHandle_t g_state_mutex;              // Mutex bảo vệ các biến trạng thái hệ thống khỏi tranh chấp dữ liệu
extern volatile int g_prediction_id;                 // ID lượt dự đoán, dùng để kích hoạt Web tự động reload ảnh mới
extern int g_last_votes[MAX_CLASSES];                // Số lượng phiếu bầu bầu chọn TinyML của lượt phân loại gần nhất
extern SemaphoreHandle_t g_buffer_mutex;             // Mutex bảo vệ các bộ đệm ảnh JPEG/RGB trong PSRAM

// ===============================================================
// HÀM TIỆN ÍCH DÙNG CHUNG
// ===============================================================

// Hàm cấp phát vùng nhớ PSRAM cho tất cả các bộ đệm ảnh
esp_err_t alloc_image_buffers(void);

// Các hàm getter/setter tương tác an toàn với bộ đệm ảnh
uint8_t* get_rgb_buffer(void);
uint8_t* get_jpeg_buffer(void); 
esp_err_t get_last_jpeg(uint8_t** out_buf, uint32_t* out_len);
void set_last_jpeg(uint8_t* buf, uint32_t len);

// Hàm ghi/đọc kết quả dự đoán TinyML (Thread-safe thông qua g_state_mutex)
void set_prediction_text(const char* text, float* probs, int* votes);
void get_prediction(char* out_text, size_t max_len, uint32_t* out_id, float* out_probs, int* out_votes);

// Hàm ghi/đọc ảnh chụp AI từng khung hình (Thread-safe thông qua g_buffer_mutex)
void set_ai_frame(int idx, uint8_t* buf, uint32_t len);
esp_err_t get_ai_frame(int idx, uint8_t** out_buf, uint32_t* out_len);

// Hàm ghi/đọc kết quả dự đoán chi tiết của từng khung hình AI
void set_frame_prediction(int idx, int winner_idx, float confidence);
void get_frame_predictions(int* out_winners, float* out_confidences, int count);

// Hàm ghi/đọc trạng thái báo đầy của hai ngăn chứa rác
void set_bin_full_status(bool b1, bool b2);
void get_bin_full_status(bool* out_b1, bool* out_b2);

#ifdef __cplusplus
}
#endif

#endif // APP_STATE_H
