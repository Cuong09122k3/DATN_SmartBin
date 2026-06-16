/**
 * servo_controller.h - Module điều khiển Servo nắp lật (ESP-IDF Port).
 *
 * Sử dụng ngoại vi LEDC (LED Control) phần cứng của ESP32-S3,
 * tần số 50Hz chuẩn cho Servo SG90/MG996R (chu kỳ 20ms).
 *
 * Ánh xạ góc → xung PWM:
 *   0   độ  →  ~500µs  →  Mở ngăn 1 (Rác nguy hại)
 *   90  độ  →  ~1450µs →  Đóng nắp (vị trí Home)
 *   180 độ  →  ~2400µs →  Mở ngăn 2 (Rác thường)
 */
#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Khởi tạo LEDC timer + channel cho Servo và đưa nắp về vị trí đóng (90 độ).
 */
void servo_controller_init(void);

/**
 * Quay Servo về 0 độ → Mở nắp đổ rác vào ngăn 1.
 */
void servo_open_bin1(void);

/**
 * Quay Servo về 180 độ → Mở nắp đổ rác vào ngăn 2.
 */
void servo_open_bin2(void);

/**
 * Quay Servo về 90 độ → Đóng nắp (vị trí trung tâm).
 */
void servo_home(void);

/**
 * Đặt góc Servo tùy ý (0-180 độ). Dùng cho tinh chỉnh.
 */
void servo_set_angle(int angle_deg);

#ifdef __cplusplus
}
#endif

#endif // SERVO_CONTROLLER_H
