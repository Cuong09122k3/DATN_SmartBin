#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

// Khởi chạy HTTP Server trên ESP32-S3 và đăng ký các đường dẫn định tuyến (URI Handlers)
void web_server_init(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H

