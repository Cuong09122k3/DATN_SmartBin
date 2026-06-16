#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khởi tạo các chân GPIO đầu vào kết nối với cảm biến hồng ngoại
void sensor_manager_init(void);

// Kiểm tra xem có rác được thả vào hay không (sử dụng cơ cấu chống nhiễu)
bool sensor_is_trash_inserted(void);



// Kiểm tra xem Ngăn 1 (Rác Pin/Nguy hại) có bị đầy hay không
bool sensor_is_bin1_full(void);

// Kiểm tra xem Ngăn 2 (Rác Thường) có bị đầy hay không
bool sensor_is_bin2_full(void);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_MANAGER_H
