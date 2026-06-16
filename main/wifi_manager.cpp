#include "wifi_manager.h"
#include "config.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

static const char* TAG = "WIFI";
static bool s_connected = false;
static bool s_auto_reconnect = true;
static esp_netif_t* s_ap_netif = NULL;
static esp_netif_t* s_sta_netif = NULL;

// Không gian lưu trữ trong NVS Flash để lưu thông tin SSID/Password WiFi
#define NVS_WIFI_NS "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"

// Trình điều phối sự kiện mạng WiFi (WiFi Event Handler)
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_auto_reconnect) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_auto_reconnect) {
            ESP_LOGW(TAG, "Mat ket noi WiFi, dang ket noi lai...");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Da duoc cap phat IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
    }
}

// Khởi động trạm tự phát WiFi Access Point (AP Mode / APSTA)
static void wifi_start_ap(void) {
    ESP_LOGW(TAG, "Dang mo diem truy cap (AP Mode / APSTA)...");
    s_auto_reconnect = false; // Vô hiệu hóa tự động kết nối khi mở AP Mode để tránh nghẽn luồng quét
    esp_wifi_stop();
    
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    
    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, "SmartBin_WiFi");
    strcpy((char*)ap_config.ap.password, "12345678");
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Tắt chế độ tiết kiệm điện năng WiFi để đạt tốc độ phản hồi Web Client cao nhất
    esp_wifi_set_ps(WIFI_PS_NONE);
    
    ESP_LOGI(TAG, "AP Mode da san sang (APSTA). SSID: SmartBin_WiFi, Pass: 12345678");
}

// Đọc thông tin SSID/Password WiFi đã lưu trong NVS Flash
static bool nvs_load_wifi(char* ssid, size_t ssid_len, char* pass, size_t pass_len) {
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NS, NVS_READONLY, &h) != ESP_OK) return false;
    
    bool ok = (nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len) == ESP_OK &&
               nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len) == ESP_OK &&
               strlen(ssid) > 0);
    nvs_close(h);
    return ok;
}

// Lưu thông tin SSID/Password WiFi mới vào bộ nhớ NVS Flash
static void nvs_save_wifi(const char* ssid, const char* pass) {
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_SSID, ssid);
        nvs_set_str(h, NVS_KEY_PASS, pass);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Da luu WiFi '%s' vao NVS.", ssid);
    }
}

// Khởi chạy trình quản lý kết nối WiFi
void wifi_manager_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    // Ưu tiên đọc cấu hình mạng đã lưu trong NVS Flash, nếu không có sẽ lấy thông tin mặc định từ config.h
    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "Dung WiFi tu NVS: '%s'", ssid);
    } else {
        strncpy(ssid, WIFI_SSID, sizeof(ssid) - 1);
        strncpy(pass, WIFI_PASSWORD, sizeof(pass) - 1);
        ESP_LOGI(TAG, "Dung WiFi mac dinh: '%s'", ssid);
    }

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Tắt chế độ tiết kiệm điện WiFi để tối ưu hóa thời gian phản hồi Web Client
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "WiFi station-mode started. Dang doi ket noi (max %ds)...", WIFI_CONNECT_TIMEOUT_S);

    // Chờ đợi kết nối trong thời gian cấu hình tối đa (WIFI_CONNECT_TIMEOUT_S giây)
    int retry = 0;
    int max_retries = WIFI_CONNECT_TIMEOUT_S * 2; // Mỗi lần quét cách nhau 500ms
    while (!s_connected && retry < max_retries) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }

    // Nếu không kết nối được WiFi trạm, tự động mở điểm phát sóng WiFi AP nội bộ
    if (!s_connected) {
        wifi_start_ap();
    }
}

// Lấy địa chỉ IP hiện tại của thiết bị (Ưu tiên IP trạm kết nối Router trước, sau đó là IP AP)
void wifi_manager_get_ip(char* out_ip) {
    esp_netif_ip_info_t ip_info;
    // Thử lấy địa chỉ IP của giao tiếp Station
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        sprintf(out_ip, IPSTR, IP2STR(&ip_info.ip));
        return;
    }
    
    // Thử lấy địa chỉ IP của giao tiếp Access Point tự phát
    netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        sprintf(out_ip, IPSTR, IP2STR(&ip_info.ip));
        return;
    }

    strcpy(out_ip, "0.0.0.0");
}

// Quét các mạng không dây xung quanh thiết bị và lưu kết quả dạng chuỗi cJSON
int wifi_manager_scan(char* out_json, size_t max_len) {
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        snprintf(out_json, max_len, "[]");
        return 0;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20; // Giới hạn quét tối đa 20 mạng để tối ưu bộ nhớ đệm

    wifi_ap_record_t* ap_list = (wifi_ap_record_t*)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_list) {
        snprintf(out_json, max_len, "[]");
        return 0;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char*)ap_list[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_list[i].rssi);
        cJSON_AddNumberToObject(item, "auth", ap_list[i].authmode);
        cJSON_AddItemToArray(arr, item);
    }
    free(ap_list);

    char* json_str = cJSON_PrintUnformatted(arr);
    strncpy(out_json, json_str, max_len - 1);
    out_json[max_len - 1] = '\0';
    free(json_str);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Quet duoc %d mang WiFi.", ap_count);
    return ap_count;
}

// Tác vụ FreeRTOS Task ngầm chuyển đổi hoàn toàn thiết bị sang chế độ STA để tiết kiệm năng lượng
static void switch_to_sta_task(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI("WIFI", "Chuyen sang STA Mode de tiet kiem pin va toi uu tai nguyen.");
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_ps(WIFI_PS_NONE); 
    vTaskDelete(NULL);
}

// Thực hiện kết nối tới Router mạng WiFi mới và lưu cấu hình vào NVS
bool wifi_manager_connect(const char* ssid, const char* password) {
    ESP_LOGI(TAG, "Dang ket noi WiFi moi: '%s'...", ssid);
    
    s_connected = false;
    s_auto_reconnect = true; // Kích hoạt tự động kết nối lại theo yêu cầu chủ động từ người dùng
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Luôn sử dụng APSTA trong lúc đang thử kết nối Router để giữ liên kết mượt mà với Web Client
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_connect();

    // Chờ kết nối thành công tối đa trong 15 giây
    bool success = false;
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (s_connected) {
            nvs_save_wifi(ssid, password);
            success = true;
            break;
        }
    }

    if (success) {
        ESP_LOGI(TAG, "Ket noi thanh cong! Se tu dong chuyen sang STA Mode trong 2 giay de toi uu...");
        // Tạo tác vụ FreeRTOS Task chuyển sang chế độ STA sau 2 giây (giúp Web Client nhận phản hồi HTTP đầy đủ)
        xTaskCreate(switch_to_sta_task, "wifi_sta_task", 2048, NULL, 3, NULL);
        return true;
    } else {
        ESP_LOGW(TAG, "Khong ket noi duoc WiFi '%s'. Quay lai AP Mode...", ssid);
        wifi_start_ap();
        return false;
    }
}

// Chuyển cưỡng bức sang chế độ tự phát WiFi AP
void wifi_manager_switch_ap(void) {
    s_connected = false;
    wifi_start_ap();
}

// Xóa trắng cấu hình WiFi trong phân vùng NVS Flash và restart vi điều khiển
void wifi_manager_clear_and_reset(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_SSID);
        nvs_erase_key(h, NVS_KEY_PASS);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Da xoa cau hinh WiFi trong NVS. Tien hanh reset thiet bi...");
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // Chờ NVS Flash ghi hoàn tất
    esp_restart();
}
