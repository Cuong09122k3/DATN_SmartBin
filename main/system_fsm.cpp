#include "system_fsm.h"
#include "app_state.h"
#include "audio_player.h"
#include "camera_manager.h"
#include "config.h"
#include "display_manager.h"
#include "inference.h"
#include "model_data.h"
#include "sensor_manager.h"
#include "servo_controller.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "FSM";

enum class FsmState { WAITING, CLASSIFYING, ACTING };

// Biến trạng thái hoạt động thực tế của FSM
static FsmState g_state = FsmState::WAITING;
static int64_t g_state_time =
    0; // Mốc thời gian lưu trữ lượt cập nhật trạng thái gần nhất

// Các biến phục vụ thuật toán bỏ phiếu (Voting Mechanism) thu thập từ 5 khung
// hình
static float g_sum_probs[MAX_CLASSES] = {0}; // Tổng điểm xác suất của từng nhãn
static int g_votes[MAX_CLASSES] = {0};       // Tổng số phiếu bầu của từng nhãn
static int g_capture_count = 0; // Đếm số khung hình đã chụp và suy luận
static int g_final_idx = 0;     // Chỉ số nhãn trúng cử chung cuộc
static float g_final_confidence =
    0.0f; // Độ tin cậy trung bình của nhãn trúng cử
static uint64_t g_detect_trigger_time =
    0; // Mốc thời gian đầu tiên phát hiện tia hồng ngoại bị cắt

// Chuyển đổi tên trạng thái FSM sang chuỗi văn bản phục vụ việc ghi nhật ký
// debug
const char *state_to_str(FsmState s) {
  switch (s) {
  case FsmState::WAITING:
    return "WAITING";
  case FsmState::CLASSIFYING:
    return "CLASSIFYING";
  case FsmState::ACTING:
    return "ACTING";
  default:
    return "UNKNOWN";
  }
}

static int64_t g_last_full_poll =
    0; // Mốc thời gian kiểm tra cảm biến đầy ngăn gần nhất
static int64_t g_last_full_alert_time =
    0; // Mốc thời gian phát âm thanh cảnh báo đầy gần nhất
static bool g_bin1_is_full = false; // Trạng thái đầy của Ngăn 1 (ổn định)
static int g_bin1_full_cnt =
    0; // Đếm số lần phát hiện đầy liên tiếp để chống nhiễu Ngăn 1
static bool g_bin2_is_full = false; // Trạng thái đầy của Ngăn 2 (ổn định)
static int g_bin2_full_cnt =
    0; // Đếm số lần phát hiện đầy liên tiếp để chống nhiễu Ngăn 2

// Cập nhật trạng thái báo đầy của các ngăn rác với bộ lọc chống nhiễu tín hiệu
// (Debounce)
static void fsm_update_bin_fullness(int64_t now) {
  if (now - g_last_full_poll > TIMING_FULL_POLL_MS) {
    g_last_full_poll = now;

    // 1. Kiểm tra chống nhiễu Ngăn 1 (Rác Pin/Nguy hại)
    bool b1_now = sensor_is_bin1_full();
    if (b1_now != g_bin1_is_full) {
      if (++g_bin1_full_cnt >= FULL_DEBOUNCE_COUNT) {
        g_bin1_is_full = b1_now;
        g_bin1_full_cnt = 0;
        if (g_bin1_is_full) {
          ESP_LOGW(TAG, "Ngan 1 DAY");
          display_show_status("THUNG 1 DAY");
          audio_play_track(AUDIO_TRACK_FULL_PIN);
          g_last_full_alert_time = now;
        } else {
          display_show_status(g_bin2_is_full ? "THUNG 2 DAY" : "CHO RAC...");
          if (!g_bin2_is_full)
            audio_play_track(AUDIO_TRACK_READY);
        }
      }
    } else {
      g_bin1_full_cnt = 0;
    }

    // 2. Kiểm tra chống nhiễu Ngăn 2 (Rác thường)
    bool b2_now = sensor_is_bin2_full();
    if (b2_now != g_bin2_is_full) {
      if (++g_bin2_full_cnt >= FULL_DEBOUNCE_COUNT) {
        g_bin2_is_full = b2_now;
        g_bin2_full_cnt = 0;
        if (g_bin2_is_full) {
          ESP_LOGW(TAG, "Ngan 2 DAY");
          display_show_status("THUNG 2 DAY");
          audio_play_track(AUDIO_TRACK_FULL_NORM);
          g_last_full_alert_time = now;
        } else {
          display_show_status(g_bin1_is_full ? "THUNG 1 DAY" : "CHO RAC...");
          if (!g_bin1_is_full)
            audio_play_track(AUDIO_TRACK_READY);
        }
      }
    } else {
      g_bin2_full_cnt = 0;
    }

    // Đồng bộ trạng thái đầy ngăn vào bộ nhớ đệm an toàn luồng để Web/OLED hiển
    // thị
    set_bin_full_status(g_bin1_is_full, g_bin2_is_full);

    // Phát âm thanh cảnh báo nhắc lại định kỳ nếu một trong hai ngăn vẫn bị đầy
    if ((g_bin1_is_full || g_bin2_is_full) &&
        (now - g_last_full_alert_time >= TIMING_FULL_ALERT_MS)) {
      audio_play_track(g_bin1_is_full ? AUDIO_TRACK_FULL_PIN
                                      : AUDIO_TRACK_FULL_NORM);
      g_last_full_alert_time = now;
    }
  }
}

// Chuyển đổi trạng thái FSM kèm theo khoảng trễ bảo vệ cơ học
void change_state(FsmState next_state) {
  if (g_state != next_state) {
    if ((next_state == FsmState::CLASSIFYING) ||
        (next_state == FsmState::ACTING)) {
      vTaskDelay(pdMS_TO_TICKS(TIMING_STATE_TRANS_MS));
    }
    ESP_LOGI(TAG, "State: %s -> %s", state_to_str(g_state),
             state_to_str(next_state));
    g_state = next_state;
    g_state_time = esp_timer_get_time() / 1000;
  }
}

// Luồng tác vụ FSM chính (FreeRTOS Task chạy song song trên Core 1)
void fsm_task(void *pvParameters) {
  vTaskDelay(
      pdMS_TO_TICKS(2000)); // Chờ các ngoại vi phần cứng ổn định dòng điện
  ESP_LOGI(TAG, "FSM Task started");

  uint8_t *rgb_buf = get_rgb_buffer();
  uint8_t *jpeg_buf = get_jpeg_buffer();

  while (1) {
    int64_t now = esp_timer_get_time() / 1000;

    switch (g_state) {
    case FsmState::WAITING: {
      // Định kỳ quét cảm biến báo đầy thùng
      fsm_update_bin_fullness(now);

      static bool already_alerted_insertion = false;

      // Nếu phát hiện có rác cắt tia hồng ngoại của cảm biến đầu vào
      if (sensor_is_trash_inserted()) {
        if (!g_bin1_is_full && !g_bin2_is_full) {
          already_alerted_insertion = false;
          static int64_t last_beep_sent_time = 0;
          if (g_detect_trigger_time == 0) {
            display_show_status("CO RAC! CHO...");
            if (now - last_beep_sent_time > 1500) {
              audio_play_track(AUDIO_TRACK_BEEP);
              last_beep_sent_time = now;
            }
            g_detect_trigger_time = now;
          } else if (now - g_detect_trigger_time >=
                     TIMING_DETECTION_CONFIRM_MS) {
            // Rác đã nằm ổn định trên khay lật đủ thời gian xác nhận (3 giây)
            display_show_status("PHAN LOAI...");
            audio_play_track(AUDIO_TRACK_DETECTED);
            audio_play_track(AUDIO_TRACK_CLASSIFYING);

            // Reset các bộ đếm phục vụ đợt phân loại AI mới
            for (int i = 0; i < MAX_CLASSES; i++) {
              g_sum_probs[i] = 0;
              g_votes[i] = 0;
            }
            g_capture_count = 0;

            // Chuyển sang tiến trình chụp ảnh và chạy suy luận AI
            change_state(FsmState::CLASSIFYING);
            g_detect_trigger_time = 0;
          }
        } else {
          // Thùng rác đã đầy, từ chối nhận thêm rác mới
          if (!already_alerted_insertion) {
            ESP_LOGW(TAG, "Thung day, tu choi nhan rac.");
            if (g_bin1_is_full)
              audio_play_track(AUDIO_TRACK_FULL_PIN);
            else
              audio_play_track(AUDIO_TRACK_FULL_NORM);

            g_last_full_alert_time = now;
            already_alerted_insertion = true;
          }
          g_detect_trigger_time = 0;
        }
      } else {
        if (g_detect_trigger_time > 0) {
          display_show_status("CHO RAC...");
        }
        already_alerted_insertion = false;
        g_detect_trigger_time = 0;
      }

      // Liên tục xả sạch bộ đệm DMA của camera để tránh bị lưu đọng ảnh cũ lúc
      // rảnh
      camera_flush_buffer(1);
      break;
    }

    case FsmState::CLASSIFYING: {
      // Chụp luân phiên các khung hình theo chu kỳ thiết lập (ví dụ: mỗi 50ms)
      if (now - g_state_time >= TIMING_CAPTURE_GAP_MS) {
        g_state_time = now;

        size_t jpeg_len = 0;
        bool captured = false;

        // Khóa bộ đệm PSRAM để ghi ảnh thu nén từ Camera
        if (xSemaphoreTake(g_buffer_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
          captured = camera_capture_to_buffers(g_capture_count, rgb_buf,
                                               jpeg_buf, &jpeg_len);
          xSemaphoreGive(g_buffer_mutex);
        }

        if (captured) {
          float probs[MAX_CLASSES] = {0}; // Tạo mảng lưu trữ xác suất
          // Chạy suy luận nhận diện TinyML trên ảnh thô RGB888 vừa thu được
          int win_idx =
              inference_run(rgb_buf, CAPTURE_COLS, CAPTURE_ROWS, probs);
          float win_prob = probs[win_idx] * 100.0f;

          // Lưu lại lịch sử dự đoán của khung hình này
          set_frame_prediction(g_capture_count, win_idx, win_prob);

          const char *frame_label = (win_idx < MODEL_NUM_CLASSES)
                                        ? MODEL_CLASS_NAMES[win_idx]
                                        : "Unknown";
          ESP_LOGI(TAG, "Frame %d/%d: %s (%.2f%%)", g_capture_count + 1,
                   CAPTURE_NUM_FRAMES, frame_label, win_prob);

          // Áp dụng bộ lọc ngưỡng tin cậy TinyML tối thiểu (ví dụ: >= 70%)
          if (win_prob >= AI_CONFIDENCE_THRESHOLD) {
            g_votes[win_idx]++;
            for (int i = 0; i < MODEL_NUM_CLASSES; i++) {
              g_sum_probs[i] += probs[i];
            }
          }

          // Đồng bộ bộ đệm ảnh của khung hình này lên PSRAM để Web hiển thị
          set_ai_frame(g_capture_count, jpeg_buf, jpeg_len);
          set_last_jpeg(jpeg_buf, jpeg_len);

          g_capture_count++;

          // Khi đã thu thập đủ 5 khung hình, tiến hành tổng hợp bỏ phiếu
          // (Voting)
          if (g_capture_count >= CAPTURE_NUM_FRAMES) {
            int final_idx = 0;
            int max_votes = -1;
            for (int i = 0; i < MODEL_NUM_CLASSES; i++) {
              if (g_votes[i] > max_votes) {
                max_votes = g_votes[i];
                final_idx = i;
              } else if (g_votes[i] == max_votes) {
                // Trường hợp hòa phiếu, ưu tiên nhãn có tổng điểm xác suất cao
                // hơn
                if (g_sum_probs[i] > g_sum_probs[final_idx])
                  final_idx = i;
              }
            }

            g_final_idx = final_idx;

            int total_valid_votes = 0; // Tính tổng số phiếu bầu hợp lệ
            for (int i = 0; i < MODEL_NUM_CLASSES; i++) {
              total_valid_votes += g_votes[i];
            }

            const char *friendly_label =
                "KHAC"; // Nhãn "KHAC" (Khác) được gán cho các loại rác không
                        // xác định hoặc không được mô hình nhận diện
            if (max_votes == 0) {
              // Trường hợp rác không xác định (không có khung hình nào vượt
              // ngưỡng tin cậy)
              g_final_idx = 0; // Đưa về nhãn Background
              g_final_confidence = 0.0f;
              friendly_label =
                  "KHONG XAC DINH"; // Nhãn "KHONG XAC DINH" (Không Xác Định)
                                    // được gán cho các loại rác không xác định
                                    // hoặc không được mô hình nhận diện
              ESP_LOGW(TAG, "Khong du frame hop le. Bo qua.");
            } else {
              g_final_confidence =
                  (total_valid_votes > 0)
                      ? (g_sum_probs[final_idx] / total_valid_votes) * 100.0f
                      : 0.0f;
              const char *raw_label = (g_final_idx < MODEL_NUM_CLASSES)
                                          ? MODEL_CLASS_NAMES[g_final_idx]
                                          : "Unknown";
              if (strstr(raw_label, "_S1"))
                friendly_label = "PIN/NGUY HAI";
              else if (strstr(raw_label, "_S2"))
                friendly_label = "RAC THUONG";
              else if (strcasecmp(raw_label, "Background") == 0)
                friendly_label = "KHONG CO RAC";
            }

            // Cập nhật kết quả lên OLED SSD1306
            display_show_result(friendly_label, g_final_confidence);

            char buf[64];
            snprintf(buf, sizeof(buf), "%s (%.2f%%)", friendly_label,
                     g_final_confidence); // Định dạng chuỗi kết quả hiển thị
                                          // (ví dụ: "RAC NHUA (85.23%)")
            float final_probs[MAX_CLASSES] = {0}; // Tạo mảng lưu trữ xác suất
            if (max_votes > 0) {
              for (int i = 0; i < MODEL_NUM_CLASSES; i++) {
                final_probs[i] =
                    (total_valid_votes > 0)
                        ? (g_sum_probs[i] / total_valid_votes) * 100.0f
                        : 0.0f;
              }
            }
            // Đồng bộ hóa kết quả an toàn luồng để Web Server cập nhật
            set_prediction_text(buf, final_probs, g_votes);

            // Xuất nhật ký thống kê bỏ phiếu TinyML
            ESP_LOGI(TAG, "=== KET QUA VOTE ===");
            for (int i = 0; i < MODEL_NUM_CLASSES; i++) {
              ESP_LOGI(TAG, "  %s: %d/%d phieu (%.2f%%)", MODEL_CLASS_NAMES[i],
                       g_votes[i], CAPTURE_NUM_FRAMES, final_probs[i]);
            }
            ESP_LOGI(TAG, ">>> Ket luan: %s (%.2f%%) <<<", friendly_label,
                     g_final_confidence);

            audio_play_track(AUDIO_TRACK_DONE);
            vTaskDelay(pdMS_TO_TICKS(TIMING_DONE_WAIT_MS));

            // Chuyển sang trạng thái chấp hành hành động cơ khí
            change_state(FsmState::ACTING);
          }
        }
      }
      break;
    }

    case FsmState::ACTING: {
      const char *label = (g_final_idx < MODEL_NUM_CLASSES)
                              ? MODEL_CLASS_NAMES[g_final_idx]
                              : "Unknown";

      // Thực hiện mở nắp ngăn chứa tương ứng và phát âm thanh hướng dẫn đổ rác
      if (strstr(label, "_S1")) {
        servo_open_bin1();
        audio_play_track(AUDIO_TRACK_PIN);
      } else if (strstr(label, "_S2")) {
        servo_open_bin2();
        audio_play_track(AUDIO_TRACK_NORMAL);
      } else {
        servo_home();
        audio_play_track(AUDIO_TRACK_BG);
      }

      // Chờ rác rơi hẳn xuống ngăn chứa (mặc định 2 giây)
      vTaskDelay(pdMS_TO_TICKS(TIMING_WAIT_DROP_MS));
      servo_home(); // Quay nắp về trạng thái đóng ban đầu

      // Khoảng trễ nghỉ ngơi (mặc định 3 giây)
      vTaskDelay(pdMS_TO_TICKS(TIMING_COOLDOWN_MS));

      display_show_status("READY/IDLE");
      audio_play_track(AUDIO_TRACK_READY);

      // Quay trở lại trạng thái chờ rác chu kỳ tiếp theo
      change_state(FsmState::WAITING);
      break;
    }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Tránh nghẽn CPU Core 1
  }
}

// Chức năng kích hoạt mở nắp cưỡng bức nhận được từ giao diện Web Client
// (Thread-safe)
bool system_fsm_trigger_manual(int bin_id) {
  // Chỉ cho phép điều khiển bằng tay khi hệ thống đang ở trạng thái rảnh (đang
  // WAITING)
  if (g_state != FsmState::WAITING) {
    ESP_LOGW(TAG, "Dieu khien qua Web bi tu choi: He thong dang ban (%s)",
             state_to_str(g_state));
    return false;
  }

  // Từ chối mở ngăn nếu cảm biến báo đầy ngăn
  if (bin_id == 1 && g_bin1_is_full) {
    ESP_LOGW(TAG, "Dieu khien qua Web bi tu choi: Ngan 1 DA DAY");
    return false;
  }
  if (bin_id == 2 && g_bin2_is_full) {
    ESP_LOGW(TAG, "Dieu khien qua Web bi tu choi: Ngan 2 DA DAY");
    return false;
  }

  ESP_LOGI(TAG, "Dieu khien qua Web duoc chap nhan: Mo ngan %d", bin_id);

  // Thiết lập các thông số giả lập để kích hoạt luồng hành động FSM
  g_final_idx = (bin_id == 1) ? 1 : 2;
  g_final_confidence = 100.0f;

  const char *friendly_label = (bin_id == 1) ? "PIN/NGUY HAI" : "RAC THUONG";
  display_show_result(friendly_label, g_final_confidence);

  char buf[128];
  snprintf(buf, sizeof(buf), "USER: %s (100.00%%)", friendly_label);

  float final_probs[MAX_CLASSES] = {0};
  final_probs[g_final_idx] = 100.0f;

  int dummy_votes[MAX_CLASSES] = {0};
  dummy_votes[g_final_idx] = CAPTURE_NUM_FRAMES;

  // Đồng bộ tức thời dữ liệu dự đoán để Web Dashboard cập nhật đồ thị ngay lập
  // tức
  set_prediction_text(buf, final_probs, dummy_votes);

  // Reset kết quả của đợt phân loại AI cũ
  for (int i = 0; i < CAPTURE_NUM_FRAMES; i++) {
    set_frame_prediction(i, -1, 0.0f);
  }

  // Kích hoạt FSM chuyển sang trạng thái hành động mở nắp
  change_state(FsmState::ACTING);

  return true;
}

// Kiểm tra xem hệ thống có đang ở trạng thái rảnh rỗi hay không
bool system_fsm_is_waiting(void) { return g_state == FsmState::WAITING; }

// Khởi chạy FreeRTOS Task ghim vào Core 1 của vi điều khiển ESP32-S3
void system_fsm_start(void) {
  xTaskCreatePinnedToCore(fsm_task, "fsm_task", 8192, NULL, 5, NULL, 1);
}
