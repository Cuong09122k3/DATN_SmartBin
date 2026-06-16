#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khởi tạo cấu hình UART và âm lượng cho module âm thanh JQ6500
void audio_player_init(void);

// Phát file âm thanh chỉ định theo ID (Sử dụng Mutex và Debounce chống nghẽn UART)
void audio_play_track(uint16_t track_id);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_PLAYER_H
