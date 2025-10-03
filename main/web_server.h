#ifndef _WEB_SERVER_H_
#define _WEB_SERVER_H_

#include "esp_mac.h"
void init_wifi(void);
esp_err_t init_web_server(void);
void web_server_init(void);
void web_get_command(uint8_t * data);

#endif