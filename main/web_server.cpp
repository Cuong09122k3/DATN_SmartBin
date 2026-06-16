#include "web_server.h"
#include "app_state.h"
#include "config.h"
#include "sensor_manager.h"
#include "camera_manager.h"
#include "model_data.h"
#include "wifi_manager.h"
#include "system_fsm.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include "cJSON.h"
#include <string.h>
#include <esp_spiffs.h>
#include <sys/stat.h>
#include <dirent.h>

// Include FreeRTOS de dung vTaskDelay
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ENABLE_WEB_CACHE false

static const char* TAG = "WEB";

// Gửi tệp tin từ phân vùng lưu trữ SPIFFS lên trình duyệt Web Client qua giao thức HTTP
static esp_err_t serve_file(httpd_req_t *req, const char* filepath, const char* content_type) {
    FILE* f = fopen(filepath, "r");
    if (f == NULL) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);
    
    // Thiết lập Cache-Control tối ưu: Bật bộ nhớ đệm 1 ngày cho file tĩnh và logo để tăng tốc độ tải trang
    if (strstr(filepath, ".css") || strstr(filepath, ".js") || strstr(filepath, ".png") || strstr(filepath, ".ico")) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400"); 
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    }

    char chunk[1024]; // Bộ đệm phân đoạn 1024 bytes tối ưu tài nguyên stack
    size_t read_bytes;
    size_t accumulated = 0;
    
    // Đọc và gửi tệp tin theo dạng phân đoạn (Chunked Response)
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0); // Giải phóng phân đoạn truyền tải khi có lỗi
            return ESP_FAIL;
        }
        accumulated += read_bytes;
        
        // Tránh nghẽn hàng đợi mạng TCP (lỗi Socket Error 11): Chờ 1 tick (10ms) sau mỗi 3KB dữ liệu truyền đi
        if (accumulated >= 3072) {
            vTaskDelay(1);
            accumulated = 0;
        }
    }
    fclose(f);
    
    // Kết thúc truyền dữ liệu phân đoạn
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Gửi ảnh JPEG từ bộ đệm RAM/PSRAM theo định dạng phân đoạn (Chunked) để tránh quá tải TCP
static esp_err_t send_buffer_chunked(httpd_req_t *req, const uint8_t* buf, size_t len, const char* content_type) {
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    size_t sent = 0;
    size_t accumulated = 0;
    char chunk[1024];
    while (sent < len) {
        size_t to_send = len - sent;
        if (to_send > sizeof(chunk)) {
            to_send = sizeof(chunk);
        }
        memcpy(chunk, buf + sent, to_send);
        if (httpd_resp_send_chunk(req, chunk, to_send) != ESP_OK) {
            httpd_resp_send_chunk(req, NULL, 0); // Hủy phân đoạn khi có lỗi socket
            return ESP_FAIL;
        }
        sent += to_send;
        accumulated += to_send;
        
        // Trì hoãn 10ms sau mỗi 3KB dữ liệu để lwIP giải phóng hàng đợi TCP
        if (accumulated >= 3072) {
            vTaskDelay(1);
            accumulated = 0;
        }
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Điều phối các tài nguyên giao diện Web tĩnh từ phân vùng SPIFFS
static esp_err_t root_get_handler(httpd_req_t *req) {
    return serve_file(req, "/spiffs/index.html", "text/html; charset=utf-8");
}

static esp_err_t style_css_get_handler(httpd_req_t *req) {
    return serve_file(req, "/spiffs/style.css", "text/css; charset=utf-8");
}

static esp_err_t script_js_get_handler(httpd_req_t *req) {
    return serve_file(req, "/spiffs/script.js", "application/javascript; charset=utf-8");
}

static esp_err_t logo_png_get_handler(httpd_req_t *req) {
    return serve_file(req, "/spiffs/60_60_utc_logo.png", "image/png");
}

// API xuất kết quả trạng thái hệ thống dưới dạng JSON (Đồng bộ thời gian thực với Dashboard Web)
static esp_err_t status_get_handler(httpd_req_t *req) {
    char pred[128]; uint32_t pred_id; float probs[MAX_CLASSES]; int votes[MAX_CLASSES];
    get_prediction(pred, sizeof(pred), &pred_id, probs, votes);
    
    // Đọc kết quả phân loại chi tiết của từng khung hình AI phục vụ vẽ đồ thị Web
    int frame_winners[CAPTURE_NUM_FRAMES];
    float frame_confs[CAPTURE_NUM_FRAMES];
    get_frame_predictions(frame_winners, frame_confs, CAPTURE_NUM_FRAMES);
    
    bool b1, b2;
    get_bin_full_status(&b1, &b2);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "bin1_full", b1);
    cJSON_AddBoolToObject(root, "bin2_full", b2);
    cJSON_AddBoolToObject(root, "busy", !system_fsm_is_waiting());
    
    char ip[20] = {0};
    wifi_manager_get_ip(ip);
    cJSON_AddStringToObject(root, "ip", ip);

    cJSON_AddStringToObject(root, "prediction", pred);
    cJSON_AddNumberToObject(root, "prediction_id", (double)pred_id);
    cJSON_AddNumberToObject(root, "display_frames", (double)CAPTURE_NUM_FRAMES);
    cJSON_AddNumberToObject(root, "threshold", (double)AI_CONFIDENCE_THRESHOLD);
    
    // Đóng gói mảng phân phối xác suất và số phiếu bầu biểu quyết TinyML
    cJSON *prob_obj = cJSON_CreateObject();
    for(int i=0; i<MODEL_NUM_CLASSES; i++) {
        const char* raw_name = MODEL_CLASS_NAMES[i];
        const char* display_name = "Khac";
        
        if (strstr(raw_name, "_S1")) display_name = "Pin (Nguy hai)";
        else if (strstr(raw_name, "_S2")) display_name = "Rac Thuong";
        else if (strcasecmp(raw_name, "Background") == 0) display_name = "Khong Co Rac";
        
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "prob", (double)(probs[i]));
        cJSON_AddNumberToObject(item, "vote", (double)(votes[i]));
        cJSON_AddItemToObject(prob_obj, display_name, item);
    }
    cJSON_AddItemToObject(root, "probs", prob_obj);
    
    // Đóng gói kết quả dự đoán riêng của từng khung hình cụ thể
    cJSON *frames_arr = cJSON_CreateArray();
    for(int i=0; i<CAPTURE_NUM_FRAMES; i++) {
        cJSON *frame = cJSON_CreateObject();
        int w = frame_winners[i];
        const char* fname = "---";
        if (w >= 0 && w < MODEL_NUM_CLASSES) {
            const char* raw = MODEL_CLASS_NAMES[w];
            if (strstr(raw, "_S1")) fname = "Pin";
            else if (strstr(raw, "_S2")) fname = "Thuong";
            else if (strcasecmp(raw, "Background") == 0) fname = "BG";
            else fname = raw;
        }
        cJSON_AddStringToObject(frame, "label", fname);
        cJSON_AddNumberToObject(frame, "conf", (double)frame_confs[i]);
        cJSON_AddItemToArray(frames_arr, frame);
    }
    cJSON_AddItemToObject(root, "frames", frames_arr);
    
    const char *sys_json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, sys_json, HTTPD_RESP_USE_STRLEN);
    free((void*)sys_json);
    cJSON_Delete(root);
    return ESP_OK;
}

// API truyền tải ảnh chụp thủ công lên Web Client
static esp_err_t photo_get_handler(httpd_req_t *req) {
    uint8_t* buf; uint32_t len;
    if (get_last_jpeg(&buf, &len) == ESP_OK) { 
        return send_buffer_chunked(req, buf, len, "image/jpeg");
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

// API truyền tải ảnh chụp của khung hình AI chỉ định lên Web Dashboard
static esp_err_t photo_infer_get_handler(httpd_req_t *req) {
    char*  buf;
    size_t buf_len;
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "id", param, sizeof(param)) == ESP_OK) {
                int idx = atoi(param);
                uint8_t* img; uint32_t len;
                if (get_ai_frame(idx, &img, &len) == ESP_OK) {
                    free(buf);
                    return send_buffer_chunked(req, img, len, "image/jpeg");
                }
            }
        }
        free(buf);
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

// API nhận lệnh yêu cầu chụp ảnh tức thời từ xa
static esp_err_t capture_post_handler(httpd_req_t *req) {
    uint8_t* buf = get_jpeg_buffer();
    size_t len = 0;

    camera_capture_manual(buf, &len);
    if (len > 0) {
        set_last_jpeg(buf, (uint32_t)len);
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Loi khi chup anh");
    return ESP_FAIL;
}

// API nhận lệnh kích hoạt mở nắp thùng rác cưỡng bức qua giao diện điều khiển Web
static esp_err_t servo_control_post_handler(httpd_req_t *req) {
    char*  buf;
    size_t buf_len;
    int bin_id = 0;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "bin", param, sizeof(param)) == ESP_OK) {
                bin_id = atoi(param);
            }
        }
        free(buf);
    }

    if (bin_id != 1 && bin_id != 2) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Tham so bin khong hop le (chi nhan 1 hoac 2)");
        return ESP_FAIL;
    }

    bool success = system_fsm_trigger_manual(bin_id);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", success);
    if (success) {
        cJSON_AddStringToObject(resp, "message", "Kich hoat mo ngan thanh cong");
    } else {
        cJSON_AddStringToObject(resp, "message", "He thong dang ban hoac ngan chua da day!");
    }

    char* resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    cJSON_Delete(resp);

    return ESP_OK;
}

// === CẤU HÌNH CÁC ĐƯỜNG DẪN ĐỊNH TUYẾN WIFI CONFIG OVER HTTP ===

// API quét tìm mạng WiFi xung quanh thiết bị
static esp_err_t wifi_scan_handler(httpd_req_t *req) {
    char* json = (char*)malloc(2048); // Khởi tạo vùng đệm 2KB trên heap tránh tràn stack
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    wifi_manager_scan(json, 2048);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

// API kết nối thùng rác tới Router mạng WiFi mới
static esp_err_t wifi_connect_handler(httpd_req_t *req) {
    char body[256] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    cJSON* root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char* ssid = cJSON_GetStringValue(cJSON_GetObjectItem(root, "ssid"));
    const char* pass = cJSON_GetStringValue(cJSON_GetObjectItem(root, "password"));
    if (!ssid || strlen(ssid) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }

    bool ok = wifi_manager_connect(ssid, pass ? pass : "");
    cJSON_Delete(root);

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", ok);
    if (ok) {
        char ip[20] = {0};
        wifi_manager_get_ip(ip);
        cJSON_AddStringToObject(resp, "ip", ip);
    }
    char* resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// API chuyển sang chế độ tự phát WiFi AP
static esp_err_t wifi_ap_handler(httpd_req_t *req) {
    wifi_manager_switch_ap();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true,\"ssid\":\"SmartBin_WiFi\",\"password\":\"12345678\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Luồng FreeRTOS Task ngầm phục vụ việc khởi động lại chip sau 1 giây
static void reset_task(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    wifi_manager_clear_and_reset();
    vTaskDelete(NULL);
}

// API xóa cấu hình WiFi đã lưu và khởi động lại thiết bị
static esp_err_t wifi_reset_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    
    // Khởi tạo task ngầm reset chip sau 1 giây để phản hồi HTTP gửi về Client kịp hoàn thành
    xTaskCreate(reset_task, "reset_task", 2048, NULL, 3, NULL);
    return ESP_OK;
}

// Khởi chạy phân vùng lưu trữ SPIFFS và thiết lập tham số khởi động HTTP Server
void web_server_init(void) {
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao SPIFFS (%s)", esp_err_to_name(ret));
        return; 
    }

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;       // Tăng stack size của httpd task lên 8KB để tránh Stack Overflow khi gửi ảnh dung lượng lớn
    config.max_open_sockets = 7;    // Hỗ trợ tối đa 7 thiết bị truy cập Dashboard đồng thời
    config.lru_purge_enable = true; // Kích hoạt cơ chế tự động giải phóng socket cũ để ưu tiên kết nối mới
    config.send_wait_timeout = 10;  // Thời gian chờ gửi tối đa là 10 giây
    config.recv_wait_timeout = 10;  // Thời gian chờ nhận tối đa là 10 giây

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t style_uri = { .uri = "/style.css", .method = HTTP_GET, .handler = style_css_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &style_uri);

        httpd_uri_t script_uri = { .uri = "/script.js", .method = HTTP_GET, .handler = script_js_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &script_uri);

        httpd_uri_t logo_uri = { .uri = "/60_60_utc_logo.png", .method = HTTP_GET, .handler = logo_png_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &logo_uri);

        httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &status_uri);
        
        httpd_uri_t sensor_status_uri = { .uri = "/sensor-status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &sensor_status_uri);

        httpd_uri_t latest_result_uri = { .uri = "/latest-result", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &latest_result_uri);

        httpd_uri_t photo_uri = { .uri = "/photo", .method = HTTP_GET, .handler = photo_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &photo_uri);

        httpd_uri_t photo_infer = { .uri = "/photo_infer", .method = HTTP_GET, .handler = photo_infer_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &photo_infer);

        httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_POST, .handler = capture_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &capture_uri);

        httpd_uri_t servo_control_uri = { .uri = "/servo/control", .method = HTTP_POST, .handler = servo_control_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &servo_control_uri);

        httpd_uri_t favicon_uri = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = [](httpd_req_t* req){ return httpd_resp_send(req, NULL, 0); }, .user_ctx = NULL };
        httpd_register_uri_handler(server, &favicon_uri);

        // API cấu hình WiFi
        httpd_uri_t wifi_scan_uri = { .uri = "/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &wifi_scan_uri);

        httpd_uri_t wifi_connect_uri = { .uri = "/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &wifi_connect_uri);

        httpd_uri_t wifi_ap_uri = { .uri = "/wifi/ap", .method = HTTP_POST, .handler = wifi_ap_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &wifi_ap_uri);

        httpd_uri_t wifi_reset_uri = { .uri = "/wifi/reset", .method = HTTP_POST, .handler = wifi_reset_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &wifi_reset_uri);

        ESP_LOGI(TAG, "Web Server da san sang");
    }
}
