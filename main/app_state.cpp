#include "app_state.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include "model_data.h"

static const char* TAG = "STATE";

uint8_t *rgb_buf           = nullptr;           // Con trỏ trỏ tới bộ đệm ảnh RGB888 thô
uint8_t *ai_jpeg_buffers[CAPTURE_NUM_FRAMES] = {nullptr}; // Các bộ đệm lưu 5 ảnh JPEG chụp lúc phân loại AI
size_t   ai_jpeg_lens[CAPTURE_NUM_FRAMES]    = {0};       // Mảng lưu độ dài thực tế của 5 tệp ảnh JPEG AI
uint8_t *manual_jpeg_buf   = nullptr;           // Bộ đệm lưu ảnh JPEG chụp thủ công theo lệnh Web
size_t   manual_jpeg_len   = 0;                 // Độ dài thực tế của ảnh chụp thủ công

char g_prediction_str[128] = "Sẵn sàng...";    // Chuỗi kết quả dự đoán hiển thị trên OLED/Web
float g_last_probs[MAX_CLASSES] = {0};           // Xác suất trung bình của các nhãn rác lượt gần nhất
int g_last_votes[MAX_CLASSES] = {0};             // Số lượng phiếu bầu TinyML lượt gần nhất
SemaphoreHandle_t g_state_mutex = NULL;          // Mutex bảo vệ dữ liệu trạng thái khỏi tranh chấp luồng
volatile int g_prediction_id = 0;               // ID tự tăng của mỗi lượt phân loại rác
SemaphoreHandle_t g_buffer_mutex = NULL;         // Mutex bảo vệ các bộ đệm ảnh JPEG trong PSRAM

static bool g_bin1_full_stable = false;          // Trạng thái lọc chống nhiễu báo đầy của Ngăn 1
static bool g_bin2_full_stable = false;          // Trạng thái lọc chống nhiễu báo đầy của Ngăn 2

// Kết quả dự đoán chi tiết của từng khung hình AI phục vụ Web vẽ đồ thị phiếu bầu
static int g_frame_winners[CAPTURE_NUM_FRAMES] = {-1, -1, -1, -1, -1}; // ID nhãn chiến thắng từng frame
static float g_frame_confidences[CAPTURE_NUM_FRAMES] = {0};            // Độ tin cậy (%) từng frame

// Cấp phát bộ đệm tĩnh trên PSRAM (bộ nhớ RAM ngoài) để tránh cạn kiệt RAM nội bộ của chip
esp_err_t alloc_image_buffers(void)
{
    // Cấp phát bộ đệm ảnh RGB888 (CAPTURE_COLS * CAPTURE_ROWS * 3 bytes)
    size_t rgb_size = CAPTURE_COLS * CAPTURE_ROWS * 3;
    rgb_buf = (uint8_t*)heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb_buf) {
        ESP_LOGE(TAG, "Loi cap phat bo nho RGB!");
        return ESP_FAIL;
    }

    // Cấp phát 5 bộ đệm tĩnh chứa ảnh JPEG phục vụ truyền tải HTTP lên Web
    for (int i = 0; i < CAPTURE_NUM_FRAMES; i++) {
        ai_jpeg_buffers[i] = (uint8_t*)heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ai_jpeg_buffers[i]) {
            ESP_LOGE(TAG, "Loi cap phat bo nho JPEG [%d]!", i);
            return ESP_FAIL;
        }
    }

    // Cấp phát bộ đệm chứa ảnh chụp thủ công theo lệnh Web
    manual_jpeg_buf = (uint8_t*)heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!manual_jpeg_buf) {
        ESP_LOGE(TAG, "Loi cap phat bo nho anh thu cong!");
        return ESP_FAIL;
    }

    // Khởi tạo các Mutex bảo vệ chống tranh chấp luồng dữ liệu (Thread-safety)
    g_state_mutex = xSemaphoreCreateMutex();
    g_buffer_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "Cap phat PSRAM va Mutex thanh cong.");
    return ESP_OK;
}

// Cập nhật thông tin dự đoán mới 
void set_prediction_text(const char* text, float* probs, int* votes)
{
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strncpy(g_prediction_str, text, sizeof(g_prediction_str) - 1);
        g_prediction_str[sizeof(g_prediction_str) - 1] = '\0';
        
        if (probs) {
            for(int i=0; i<MODEL_NUM_CLASSES; i++) g_last_probs[i] = probs[i];
        }
        if (votes) {
            for(int i=0; i<MODEL_NUM_CLASSES; i++) g_last_votes[i] = votes[i];
        }
        
        g_prediction_id = g_prediction_id + 1; // Tăng ID để báo Web tải lại ảnh mới
        xSemaphoreGive(g_state_mutex);
    }
}

// Đọc thông tin dự đoán hiện tại 
void get_prediction(char* out_text, size_t max_len, uint32_t* out_id, float* out_probs, int* out_votes)
{
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (out_text) strncpy(out_text, g_prediction_str, max_len);
        if (out_id) *out_id = g_prediction_id;
        if (out_probs) {
            for(int i=0; i<MODEL_NUM_CLASSES; i++) out_probs[i] = g_last_probs[i];
        }
        if (out_votes) {
            for(int i=0; i<MODEL_NUM_CLASSES; i++) out_votes[i] = g_last_votes[i];
        }
        xSemaphoreGive(g_state_mutex);
    }
}

// Sao chép ảnh chụp AI JPEG của từng khung hình vào bộ đệm PSRAM tương ứng
void set_ai_frame(int idx, uint8_t* buf, uint32_t len) {
    if (idx < 0 || idx >= CAPTURE_NUM_FRAMES) return;
    
    if (xSemaphoreTake(g_buffer_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (ai_jpeg_buffers[idx] && buf && len > 0) {
            size_t copy_len = (len > MAX_JPEG_SIZE) ? MAX_JPEG_SIZE : len;
            memcpy(ai_jpeg_buffers[idx], buf, copy_len);
            ai_jpeg_lens[idx] = copy_len;
        }
        xSemaphoreGive(g_buffer_mutex);
    }
}

// Lấy ảnh JPEG AI tương ứng để truyền lên trình duyệt qua Web Server 
esp_err_t get_ai_frame(int idx, uint8_t** out_buf, uint32_t* out_len) {
    if (idx < 0 || idx >= CAPTURE_NUM_FRAMES) return ESP_ERR_INVALID_ARG;
    esp_err_t res = ESP_FAIL;
    
    if (xSemaphoreTake(g_buffer_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (ai_jpeg_buffers[idx] && ai_jpeg_lens[idx] > 0) {
            *out_buf = ai_jpeg_buffers[idx];
            *out_len = ai_jpeg_lens[idx];
            res = ESP_OK;
        }
        xSemaphoreGive(g_buffer_mutex);
    }
    return res;
}

uint8_t* get_rgb_buffer(void) { return rgb_buf; }
uint8_t* get_jpeg_buffer(void) { return manual_jpeg_buf; }

// Lưu trữ ảnh chụp thủ công theo yêu cầu từ Web (Thread-safe)
void set_last_jpeg(uint8_t* buf, uint32_t len) {
    if (!manual_jpeg_buf || !buf || len == 0) return;
    if (xSemaphoreTake(g_buffer_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        size_t copy_len = (len > MAX_JPEG_SIZE) ? MAX_JPEG_SIZE : len;
        memcpy(manual_jpeg_buf, buf, copy_len);
        manual_jpeg_len = copy_len;
        xSemaphoreGive(g_buffer_mutex);
    }
}

// Lấy ảnh chụp thủ công để gửi lên trình duyệt (Thread-safe)
esp_err_t get_last_jpeg(uint8_t** out_buf, uint32_t* out_len) {
    esp_err_t res = ESP_FAIL;
    if (xSemaphoreTake(g_buffer_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (manual_jpeg_buf && manual_jpeg_len > 0) {
            *out_buf = manual_jpeg_buf;
            *out_len = manual_jpeg_len;
            res = ESP_OK;
        }
        xSemaphoreGive(g_buffer_mutex);
    }
    return res;
}

// Cập nhật trạng thái báo đầy đã được xử lý chống nhiễu 
void set_bin_full_status(bool b1, bool b2) {
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_bin1_full_stable = b1;
        g_bin2_full_stable = b2;
        xSemaphoreGive(g_state_mutex);
    }
}

// Lấy trạng thái báo đầy ổn định của 2 ngăn 
void get_bin_full_status(bool* out_b1, bool* out_b2) {
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (out_b1) *out_b1 = g_bin1_full_stable;
        if (out_b2) *out_b2 = g_bin2_full_stable;
        xSemaphoreGive(g_state_mutex);
    }
}

// Lưu trữ kết quả dự đoán của từng khung hình AI cụ thể 
void set_frame_prediction(int idx, int winner_idx, float confidence) {
    if (idx < 0 || idx >= CAPTURE_NUM_FRAMES) return;
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_frame_winners[idx] = winner_idx;
        g_frame_confidences[idx] = confidence;
        xSemaphoreGive(g_state_mutex);
    }
}

// Lấy mảng dự đoán chi tiết 5 khung hình để trả về cho Web Server 
void get_frame_predictions(int* out_winners, float* out_confidences, int count) {
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int n = (count > CAPTURE_NUM_FRAMES) ? CAPTURE_NUM_FRAMES : count;
        if (out_winners) {
            for (int i = 0; i < n; i++) out_winners[i] = g_frame_winners[i];
        }
        if (out_confidences) {
            for (int i = 0; i < n; i++) out_confidences[i] = g_frame_confidences[i];
        }
        xSemaphoreGive(g_state_mutex);
    }
}
