#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khởi tạo kênh truyền thông I2C và màn hình hiển thị OLED SSD1306
bool display_init(void);

// Hiển thị trạng thái hoạt động tức thời của hệ thống (Ví dụ: "PHAN LOAI...", "CHO RAC...")
void display_show_status(const char* status);

// Hiển thị kết quả biểu quyết AI trúng cử cùng mức độ tin cậy tương ứng (%)
void display_show_result(const char* label, float confidence);



// Hiển thị thông số địa chỉ IP nội mạng của thiết bị (ở góc dưới cùng màn hình)
void display_show_network(const char* ip);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_MANAGER_H
