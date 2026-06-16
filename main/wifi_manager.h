#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo WiFi Station và kết nối.
 * Sử dụng SSID và Password trong config.h.
 */
void wifi_manager_init(void);

void wifi_manager_get_ip(char *out_ip);

/**
 * @brief Quét danh sách WiFi xung quanh, ghi vào buffer JSON.
 * @return Số mạng tìm thấy (0 nếu lỗi).
 */
int wifi_manager_scan(char *out_json, size_t max_len);

/**
 * @brief Kết nối WiFi mới, lưu SSID/Password vào NVS.
 * @return true nếu kết nối thành công.
 */
bool wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Chuyển sang chế độ AP (SmartBin_WiFi / 12345678).
 */
void wifi_manager_switch_ap(void);

/**
 * @brief Xóa thông tin WiFi đã lưu trong NVS và reset thiết bị.
 */
void wifi_manager_clear_and_reset(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
