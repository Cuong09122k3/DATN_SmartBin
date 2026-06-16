#ifndef INFERENCE_H
#define INFERENCE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khởi chạy và cấp phát Tensor Arena trên PSRAM cho bộ thông dịch TensorFlow Lite Micro
bool inference_init(void);

// Chạy suy luận nhận diện TinyML: Bilinear Resize ảnh -> Lượng tử hóa Quantize -> Gọi Invoke -> Trả về nhãn có xác suất cao nhất
int inference_run(uint8_t* raw_rgb_buffer, int img_width, int img_height, float* out_probabilities);

#ifdef __cplusplus
}
#endif

#endif // INFERENCE_H
