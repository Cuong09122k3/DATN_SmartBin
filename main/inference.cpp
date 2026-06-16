#include "inference.h"
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "AI";

namespace {
const tflite::Model *model =
    nullptr; // Con trỏ trỏ tới mô hình mạng nơ-ron TinyML
tflite::MicroInterpreter *interpreter =
    nullptr; // Bộ thông dịch mô hình TFLite Micro
TfLiteTensor *input =
    nullptr; // Con trỏ tới Tensor đầu vào (mảng ảnh lượng tử hóa)
TfLiteTensor *output =
    nullptr; // Con trỏ tới Tensor đầu ra (mảng xác suất các nhãn)
const int kTensorArenaSize =
    1536 * 1024; // Kích thước bộ nhớ đệm chạy suy luận (1.5MB Tensor Arena)
uint8_t *tensor_arena = nullptr; // Vùng nhớ Arena cấp phát tĩnh
} // namespace

bool inference_init(void) {
  // 1. Lấy mô hình từ mảng phẳng g_model lưu trong bộ nhớ Flash
  model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(TAG, "Model version mismatch (%d vs %d).", (int)model->version(),
             TFLITE_SCHEMA_VERSION);
    return false;
  }

  // 2. Cấp phát vùng nhớ Tensor Arena lớn (1.5MB) trên bộ nhớ RAM ngoài PSRAM
  // để tránh tràn RAM nội bộ của chip
  tensor_arena = (uint8_t *)heap_caps_malloc(
      kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (tensor_arena == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM for AI!");
    return false;
  }

  // 3. Đăng ký các toán tử (Operators) cần thiết dùng trong cấu trúc mô hình
  static tflite::MicroMutableOpResolver<20> resolver;
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddReshape();
  resolver.AddAdd();
  resolver.AddSub();
  resolver.AddMul();
  resolver.AddMaxPool2D();
  resolver.AddAveragePool2D();
  resolver.AddPad();
  resolver.AddStridedSlice();
  resolver.AddMean();
  resolver.AddQuantize();
  resolver.AddDequantize();

  // 4. Khởi tạo Interpreter thông dịch mô hình tĩnh
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // 5. Cấp phát bộ nhớ cho các Tensors đầu vào/đầu ra
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(TAG, "AllocateTensors() failed!");
    return false;
  }

  // Trích xuất con trỏ trỏ tới Tensor đầu vào và đầu ra để truyền nhận dữ liệu
  input = interpreter->input(0);
  output = interpreter->output(0);

  ESP_LOGI(TAG, "AI initialized. Arena: %d bytes (PSRAM).", kTensorArenaSize);
  return true;
}

// Thuật toán nội suy song tuyến (Bilinear Interpolation) kết hợp Lượng tử hóa
// (Quantization) Thu nhỏ kích thước ảnh chụp gốc sang kích thước đầu vào của mô
// hình, đồng thời chuyển uint8 sang int8 lượng tử hóa
static void resize_and_quantize(uint8_t *src, int src_w, int src_h, int8_t *dst,
                                int dst_w, int dst_h) {
  const float scale_x = (float)(src_w) / dst_w;
  const float scale_y = (float)(src_h) / dst_h;

  // Đọc các tham số lượng tử hóa của mô hình TinyML
  const float q_scale = input->params.scale;
  const int q_zero = input->params.zero_point;
  const float inv_q_scale = 1.0f / q_scale;

  for (int y = 0; y < dst_h; ++y) {
    float src_yf = (y + 0.5f) * scale_y - 0.5f;
    int y0 = (int)src_yf;
    if (y0 < 0)
      y0 = 0;
    int y1 = (y0 + 1 >= src_h) ? src_h - 1 : y0 + 1;
    float dy = src_yf - y0;
    float inv_dy = 1.0f - dy;

    uint8_t *row0 = &src[y0 * src_w * 3];
    uint8_t *row1 = &src[y1 * src_w * 3];

    for (int x = 0; x < dst_w; ++x) {
      float src_xf = (x + 0.5f) * scale_x - 0.5f;
      int x0 = (int)src_xf;
      if (x0 < 0)
        x0 = 0;
      int x1 = (x0 + 1 >= src_w) ? src_w - 1 : x0 + 1;
      float dx = src_xf - x0;
      float inv_dx = 1.0f - dx;

      // Tính toán trọng số cho 4 điểm ảnh lân cận
      float w00 = inv_dx * inv_dy;
      float w10 = dx * inv_dy;
      float w01 = inv_dx * dy;
      float w11 = dx * dy;

      int dst_idx = (y * dst_w + x) * 3;
      for (int c = 0; c < 3; ++c) {
        float p00 = row0[x0 * 3 + c];
        float p10 = row0[x1 * 3 + c];
        float p01 = row1[x0 * 3 + c];
        float p11 = row1[x1 * 3 + c];

        // Tính toán giá trị pixel nội suy song tuyến (Bilinear Interpolated
        // Pixel)
        float pixel_val = p00 * w00 + p10 * w10 + p01 * w01 + p11 * w11;

        // Lượng tử hóa pixel về miền số nguyên 8-bit có dấu (int8) dựa trên
        // Zero Point và Scale
        int q_val = (int)(pixel_val * inv_q_scale) + q_zero;

        // Khống chế biên giới miền int8 [-128, 127]
        if (q_val > 127)
          q_val = 127;
        else if (q_val < -128)
          q_val = -128;
        dst[dst_idx + c] = (int8_t)q_val;
      }
    }
  }
}

// Chạy suy luận nhận dạng phân loại rác dựa trên ảnh RGB888
int inference_run(uint8_t *raw_rgb_buffer, int img_width, int img_height,
                  float *out_probabilities) {
  if (interpreter == nullptr)
    return -1;

  // 1. Chạy thuật toán resize ảnh và lượng tử hóa trực tiếp vào Tensor đầu vào
  // của Interpreter
  resize_and_quantize(raw_rgb_buffer, img_width, img_height, input->data.int8,
                      MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT);

  // 2. Kích hoạt thông dịch mạng AI để thực hiện suy luận (đo thời gian suy
  // luận thực tế tính bằng ms)
  int64_t start_time = esp_timer_get_time();
  if (interpreter->Invoke() != kTfLiteOk) {
    ESP_LOGE(TAG, "Invoke() failed!");
    return -1;
  }
  int duration_ms = (int)((esp_timer_get_time() - start_time) / 1000);

  int num_classes = MODEL_NUM_CLASSES;
  int max_idx = 0;
  float max_prob = -1.0f;

  char log_buf[128];
  int offset = 0;
  offset += snprintf(log_buf + offset, sizeof(log_buf) - offset, "AI Probs: ");

  // 3. Đọc dữ liệu từ Tensor đầu ra và Giải lượng tử hóa (Dequantize) sang xác
  // suất thực tế dạng số thực %
  for (int i = 0; i < num_classes; i++) {
    int quantized_val = output->data.int8[i];
    // Công thức giải lượng tử hóa: Real_Val = (Quantized_Val - Zero_Point) *
    // Scale
    float real_prob =
        (quantized_val - output->params.zero_point) * output->params.scale;

    if (out_probabilities != nullptr)
      out_probabilities[i] = real_prob;

    offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                       "[C%d:%.2f%%] ", i, real_prob * 100.0f);

    // Tìm nhãn rác có xác suất chiến thắng cao nhất
    if (real_prob > max_prob) {
      max_prob = real_prob;
      max_idx = i;
    }
  }
  ESP_LOGI(TAG, "%s (%d ms)", log_buf, duration_ms);
  return max_idx; // Trả về ID lớp rác chiến thắng
}
