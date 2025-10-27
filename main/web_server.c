
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include "esp_http_server.h"
#include "web_server.h"
#include "cJSON.h" // Assume cJSON component is included in the project
#include "lin_bar.h"
#include "egg.h"
#include "system.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "password.h"
#include "patterns.h"

#define FILENAME_LEN 520
static const char *TAG = "ESP32_WEB_SERVER";

static httpd_handle_t web_server = NULL;
static int ws_client_fd = -1;

// Arrays for current values
static int slider_values[6] = {0};
static bool switch_states[3] = {0};
static int indicator_values[9] = {0};

// Callback types
typedef void (*slider_change_cb_t)(int id, int value);
typedef void (*switch_change_cb_t)(int id, bool state);
typedef void (*client_disconnect_cb_t)(void);

// Callback arrays (user can register callbacks)
static slider_change_cb_t slider_callbacks[6] = {NULL};
static switch_change_cb_t switch_callbacks[3] = {NULL};
static client_disconnect_cb_t disconnect_callback = NULL;

static lin_bar_command_t web_cmd;

void slider_cb(int id, int value) {
    ESP_LOGI(TAG, "Slider %d is %d", id, value);
    switch (id) {
        case 0:
            web_cmd.values.value0 = value;
            break;
        case 1:
            web_cmd.values.value1 = value;
            break;
        case 2:
            web_cmd.values.value2 = value;
            break;
        case 3:
            web_cmd.values.value3 = value;
            break;
        case 4:
            web_cmd.values.value4 = value;
            break;
        case 5:
            web_cmd.values.value5 = value;
            break;
    };
    egg_msg_handler();    
}
void web_get_command(uint8_t * data) {
    memcpy(data, web_cmd.bytes, 8);
}
void switch_cb(int id, bool state) {
    ESP_LOGI(TAG, "Switch %d is %d", id, state?1:0);        
}

// Function to register callbacks
void register_slider_callback(int id, slider_change_cb_t cb) {
    if (id >= 0 && id < 6) {
        slider_callbacks[id] = cb;
    }
}

void register_switch_callback(int id, switch_change_cb_t cb) {
    if (id >= 0 && id < 3) {
        switch_callbacks[id] = cb;
    }
}

void register_disconnect_callback(client_disconnect_cb_t cb) {
    disconnect_callback = cb;
}

// Functions to set values (do not push immediately)
void set_slider(int id, int value) {
    if (id >= 0 && id < 6) {
        slider_values[id] = value > 100 ? 100 : (value < 0 ? 0 : value);
    }
}

void set_switch(int id, bool state) {
    if (id >= 0 && id < 3) {
        switch_states[id] = state;
    }
}

void set_indicator(int id, int value) {
    if (id >= 0 && id < 9) {
        indicator_values[id] = value > 100 ? 100 : (value < 0 ? 0 : value);
    }
}

// Function to send WebSocket message asynchronously
typedef struct {
    uint8_t *payload;
    size_t len;
} ws_message_t;

static void ws_async_send(void *arg) {
    ws_message_t *msg = (ws_message_t *)arg;
    if (ws_client_fd != -1) {
        httpd_ws_frame_t ws_frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = msg->payload,
            .len = msg->len
        };
        esp_err_t ret = httpd_ws_send_frame_async(web_server, ws_client_fd, &ws_frame);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WebSocket send failed: %d", ret);
        }
    }
    free(msg->payload);
    free(msg);
}

static void send_ws_message(const char *data, size_t len) {
    if (ws_client_fd == -1) return;
    ws_message_t *msg = malloc(sizeof(ws_message_t));
    if (msg == NULL) return;
    msg->payload = malloc(len);
    if (msg->payload == NULL) {
        free(msg);
        return;
    }
    memcpy(msg->payload, data, len);
    msg->len = len;
    httpd_queue_work(web_server, ws_async_send, msg);
}

// Function to push all UI changes to the client
void push_ui_changes() {
    char json[128];
    for (int i = 0; i < 6; i++) {
        snprintf(json, sizeof(json), "{\"type\":\"set_slider\",\"id\":%d,\"value\":%d}", i, slider_values[i]);
        send_ws_message(json, strlen(json));
    }
    for (int i = 0; i < 3; i++) {
        snprintf(json, sizeof(json), "{\"type\":\"set_switch\",\"id\":%d,\"value\":%d}", i, switch_states[i] ? 1 : 0);
        send_ws_message(json, strlen(json));
    }
    for (int i = 0; i < 9; i++) {
        snprintf(json, sizeof(json), "{\"type\":\"set_indicator\",\"id\":%d,\"value\":%d}", i, indicator_values[i]);
        send_ws_message(json, strlen(json));
    }
}
static const char *get_content_type_from_uri(const char *uri) {
    const char *dot = strrchr(uri, '.');
    if (!dot || strcmp(uri, "/") == 0) return "text/html";
    if (strcmp(dot, ".html") == 0) return "text/html";
    if (strcmp(dot, ".css") == 0) return "text/css";
    if (strcmp(dot, ".js") == 0) return "application/javascript";
    if (strcmp(dot, ".jpg") == 0) return "image/jpeg";
    return "text/plain";
}

static esp_err_t file_get_handler(httpd_req_t *req) {
    char filepath[FILENAME_LEN + 1];
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "/spiffs/dash.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/spiffs%s", uri);
    }

    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_content_type_from_uri(uri));

    char buf[1024];
    size_t bytes_read;
    do {
        bytes_read = fread(buf, 1, sizeof(buf), fd);
        if (bytes_read > 0) {
            if (httpd_resp_send_chunk(req, buf, bytes_read) != ESP_OK) {
                fclose(fd);
                return ESP_FAIL;
            }
        }
    } while (bytes_read > 0);

    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


// Login page HTML
static const char *login_html =
    "<html><body><form action='/login' method='post'>"
    "Username: <input type='text' name='user'><br>"
    "Password: <input type='password' name='pass'><br>"
    "<input type='submit' value='Login'>"
    "</form></body></html>";

// Dashboard HTML (with sliders, switches, indicators)
static const char *dash_html =
    "<html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body {"
    "font-family: Arial, sans-serif;"
    "background-color: #f0f0f0;"
    "display: flex;"
    "flex-direction: row;"
    "align-items: center;"
    "justify-content: center;"
    "gap: 5px;"
    "overflow-x: auto;"
    "}"
    ".indicator {width:20px; height:20px; background:red; display:inline-block; vertical-align:middle;}"

    "input[type='range'] {"
    "width:200px !important;"
    "height:50px !important;"
    "orientation: vertical !important;"
    "-webkit-appearance: slider-vertical !important;"
    "writing-mode: bt-lr !important;"
    "transform: rotate(270deg);"
    "transform-origin: center;"
    "margin: 5px 0;"
    "}"
    ".slider-container {display: flex; flex-direction: column; align-items: center; gap: 5px; width: 50px; height: 500px;}"
    "</style></head><body>"
 //   "<h1>Control Panel</h1>"
    "<div class='slider-container'>"
        "<div>Slider 0: <input type='range' id='slider0' min='0' max='100' value='0' onchange='sendChange(\"slider\",0,this.value)'> "
        "<div class='indicator' id='ind0'></div></div><br>"
    "</div>"
    "<div class='slider-container'>"
      "<div>Slider 1: <input type='range' id='slider1' min='0' max='100' value='0' onchange='sendChange(\"slider\",1,this.value)'>"
        " <div class='indicator' id='ind1'></div></div><br>"
    "</div>"
    "<div class='slider-container'>"
       "<div>Slider 2: <input type='range' id='slider2' min='0' max='100' value='0' onchange='sendChange(\"slider\",2,this.value)'>"
        " <div class='indicator' id='ind2'></div></div><br>"
    "</div>"
    "<div class='slider-container'>"
      "<div>Slider 3: <input type='range' id='slider3' min='0' max='100' value='0' onchange='sendChange(\"slider\",3,this.value)'>"
        " <div class='indicator' id='ind3'></div></div><br>"
    "</div>"
    "<div class='slider-container'>"
      "<div>Slider 4: <input type='range' id='slider4' min='0' max='100' value='0' onchange='sendChange(\"slider\",4,this.value)'>"
        " <div class='indicator' id='ind4'></div></div><br>"
    "</div>"
    "<div class='slider-container'>"
       "<div>Slider 5: <input type='range' id='slider5' min='0' max='100' value='0' onchange='sendChange(\"slider\",5,this.value)'>"
        " <div class='indicator' id='ind5'></div></div><br>"
    "</div>"
#if 0
    "<div>Switch 0: <input type='checkbox' id='switch0' onchange='sendChange(\"switch\",0,this.checked?1:0)'> <div class='indicator' id='ind6'></div></div><br>"
    "<div>Switch 1: <input type='checkbox' id='switch1' onchange='sendChange(\"switch\",1,this.checked?1:0)'> <div class='indicator' id='ind7'></div></div><br>"
    "<div>Switch 2: <input type='checkbox' id='switch2' onchange='sendChange(\"switch\",2,this.checked?1:0)'> <div class='indicator' id='ind8'></div></div><br>"
#endif
    "<script>"
    "var ws = new WebSocket('ws://' + location.host + '/ws');"
    "function sendChange(type, id, value) {"
    " ws.send(JSON.stringify({type: type, id: id, value: parseInt(value)}));"
    "}"
    "ws.onmessage = function(evt) {"
    " var data = JSON.parse(evt.data);"
    " if (data.type === 'set_slider') {"
    " document.getElementById('slider' + data.id).value = data.value;"
    " } else if (data.type === 'set_switch') {"
    " document.getElementById('switch' + data.id).checked = data.value > 0;"
    " } else if (data.type === 'set_indicator') {"
    " document.getElementById('ind' + data.id).style.opacity = data.value / 100;"
    " }"
    "};"
    "ws.onclose = function() {"
    " console.log('WebSocket connection closed');"
    "};"
    "</script></body></html>";

// Handler for GET /
static esp_err_t login_get_handler(httpd_req_t *req) {
    char hdr[128];
    if (httpd_req_get_hdr_value_str(req, "Cookie", hdr, sizeof(hdr)) == ESP_OK &&
        strstr(hdr, "auth=1") != NULL) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/dash");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    httpd_resp_send(req, login_html, strlen(login_html));
    return ESP_OK;
}

// Simple URL decode (for form data)
static void url_decode(char *str) {
    char *p = str;
    while (*str) {
        if (*str == '+') {
            *p++ = ' ';
            str++;
        } else if (*str == '%' && *(str + 1) && *(str + 2)) {
            char hex[3] = {*(str + 1), *(str + 2), 0};
            *p++ = (char)strtol(hex, NULL, 16);
            str += 3;
        } else {
            *p++ = *str++;
        }
    }
    *p = 0;
}

// Handler for POST /login
static esp_err_t login_post_handler(httpd_req_t *req) {
    char body[256];
    int len = req->content_len;
    if (len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Too large");
        return ESP_FAIL;
    }
    if (httpd_req_recv(req, body, len) != len) {
        return ESP_FAIL;
    }
    body[len] = 0;

    char *user = strstr(body, "user=");
    char *pass = strstr(body, "pass=");
    bool valid = false;

    if (user && pass) {
        user += 5;
        char *amp = strchr(user, '&');
        if (amp) *amp = 0;
        pass += 5;
        url_decode(user);
        url_decode(pass);
        if (strcmp(user, "admin") == 0 && strcmp(pass, "password") == 0) {
            valid = true;
        }
    }

    if (valid) {
        httpd_resp_set_hdr(req, "Set-Cookie", "auth=1; Path=/");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/dash");
        httpd_resp_send(req, NULL, 0);
    } else {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
    }
    return ESP_OK;
}

// Handler for GET /dash
static esp_err_t dash_get_handler(httpd_req_t *req) {
    char hdr[128];
    if (httpd_req_get_hdr_value_str(req, "Cookie", hdr, sizeof(hdr)) != ESP_OK ||
        strstr(hdr, "auth=1") == NULL) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    httpd_resp_send(req, dash_html, strlen(dash_html));
    return ESP_OK;
}

// WebSocket handler
static esp_err_t ws_handler(httpd_req_t *req) {
    char hdr[128];
    #if 0
    if (httpd_req_get_hdr_value_str(req, "Cookie", hdr, sizeof(hdr)) != ESP_OK ||
        strstr(hdr, "auth=1") == NULL) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }
        #endif

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake done, the new connection was opened");
        ws_client_fd = httpd_req_to_sockfd(req);
        // Push initial UI state on connect
        push_ui_changes();
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t buf[1024] = {0};
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = buf;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket receive failed: %d", ret);
#if 0
        if (ret == HTTPD_WS_ERR_DISCONNECTED) {
            ESP_LOGI(TAG, "WebSocket client disconnected");
            if (ws_client_fd != -1) {
                if (disconnect_callback) {
                    disconnect_callback();
                }
                ws_client_fd = -1;
            }
        }
#endif
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket client sent close frame");
        if (ws_client_fd != -1) {
            if (disconnect_callback) {
                disconnect_callback();
            }
            ws_client_fd = -1;
        }
        return ESP_OK;
    }

    if (ws_pkt.len > 0) {
        buf[ws_pkt.len] = 0;
        cJSON *json = cJSON_Parse((const char *)buf);
        if (json) {
            cJSON *type_item = cJSON_GetObjectItem(json, "type");
            if (type_item && cJSON_IsString(type_item)) {
                char *type = type_item->valuestring;
                cJSON *id_item = cJSON_GetObjectItem(json, "id");
                cJSON *value_item = cJSON_GetObjectItem(json, "value");
                if (id_item && value_item && cJSON_IsNumber(id_item) && cJSON_IsNumber(value_item)) {
                    int id = id_item->valueint;
                    int value = value_item->valueint;
                    ESP_LOGI(TAG, "type: %s id:%d value:%d", type, id, value);
                    if (strcmp(type, "slider") == 0 && id >= 0 && id < 6) {
                        ESP_LOGI(TAG,"Slider");
                        slider_values[id] = value;
                        if (slider_callbacks[id]) {
                            slider_callbacks[id](id, value);
                        }
                    } else if (strcmp(type, "switch") == 0 && id >= 0 && id < 3) {
                        ESP_LOGI(TAG,"Switch");
                        switch_states[id] = (value > 0);
                        if (switch_callbacks[id]) {
                            switch_callbacks[id](id, switch_states[id]);
                        }
                    } else if (strcmp(type, "btn") == 0) {
                        ESP_LOGD(TAG, "Button");
                        system_load_set(id, value);
                        //mesh handler checks                                                 
                    } else if (strcmp(type, "mode") == 0) {
                        switch(id) {
                            case 0:
                                sequenceSelect(SEQ_IDLE);
                                break;
                            case 1:
                                sequenceSelect(SEQ_WIG_WAG);
                                break;
                            case 2:
                                sequenceSelect(SEQ_SWEEP);
                                break;
                            case 3:
                                sequenceSelect(SEQ_KITT);
                                break;
                            default:
                                sequenceSelect(SEQ_IDLE);
                                break;
                        }
                    }
                }
            }
            cJSON_Delete(json);
        }
    }

    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station connected");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Station disconnected");
        if (ws_client_fd != -1) {
            if (disconnect_callback) {
                disconnect_callback();
            }
            ws_client_fd = -1;
        }
    }
}

void init_wifi(void) {
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    char * ap_name = system_get_name();
    if (ap_name == NULL) {
        ap_name = WIFI_AP;
    }
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ap_name),
            .channel = 1,
            .password = WIFI_AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    strcpy((char *) wifi_config.ap.ssid, ap_name);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 5,1);
    IP4_ADDR(&ip_info.gw, 192, 168, 5,1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s", ap_name, WIFI_AP_PASS);
}

esp_err_t init_web_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&web_server, &config);
    if (ret == ESP_OK) {
        httpd_uri_t login_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = file_get_handler
        };
        httpd_register_uri_handler(web_server, &login_get);

        httpd_uri_t login_post = {
            .uri = "/login",
            .method = HTTP_POST,
            .handler = login_post_handler
        };
        httpd_register_uri_handler(web_server, &login_post);

        httpd_uri_t dash_get = {
            .uri = "/dash",
            .method = HTTP_GET,
            .handler = file_get_handler
        };
        httpd_register_uri_handler(web_server, &dash_get);

        httpd_uri_t ws = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .is_websocket = true
        };
        httpd_register_uri_handler(web_server, &ws);

        httpd_uri_t css_uri = {
            .uri = "/style.css",
            .method = HTTP_GET,
            .handler = file_get_handler
        };
        httpd_register_uri_handler(web_server, &css_uri);

        httpd_uri_t js_uri = {
            .uri = "/dash.js",
            .method = HTTP_GET,
            .handler = file_get_handler
        };
        httpd_register_uri_handler(web_server, &js_uri);

        httpd_uri_t img_uri = {
            .uri = "/image.jpg",
            .method = HTTP_GET,
            .handler = file_get_handler
        };
        httpd_register_uri_handler(web_server, &img_uri);

    }
    return ret;
}


void on_client_disconnect(void) {
    ESP_LOGI(TAG, "Client disconnected, performing cleanup...");
    // Add custom cleanup logic here
}

void ap_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    init_wifi();
}

void web_server_init(void) {
    esp_err_t ret;
    ESP_LOGI(TAG," setting callbacks");

    for (int i = 0; i < 6; i++) {
        register_slider_callback(i, slider_cb);
    }
    for (int i = 0; i < 3; i++) {
        register_switch_callback(i, switch_cb);
    }
    
    register_disconnect_callback(on_client_disconnect);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "SPIFFS partition size: total:%d, used:%d", total, used);


    init_web_server();
}