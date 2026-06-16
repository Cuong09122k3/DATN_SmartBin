#ifndef SYSTEM_FSM_H
#define SYSTEM_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

// Khởi tạo luồng máy trạng thái hệ thống chạy song song (FreeRTOS Task)
void system_fsm_start(void);

// Kích hoạt cưỡng bức hệ thống chuyển sang trạng thái phân loại rác thủ công (qua giao diện Web)
bool system_fsm_trigger_manual(int bin_id);

// Kiểm tra xem hệ thống có đang ở trạng thái rảnh/đợi rác hay không
bool system_fsm_is_waiting(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_FSM_H

