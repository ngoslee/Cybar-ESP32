#include "web_mesh.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lin_bar.h"

static const char *TAG = "main_ap";
static char statuses[1024] = "No statuses yet.";
static bool link_up = false;

static void udp_server_task(void *pvParameters) {
    struct sockaddr_in dest_addr;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
    }
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(3333);
    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "Socket bound, waiting for data...");
    while (1) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        char rx_buffer[512];
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &addr_len);
        if (len > 0) {
            rx_buffer[len] = 0;
            strncpy(statuses, rx_buffer, sizeof(statuses) - 1);
            ESP_LOGI(TAG, "Received status update from root %s", statuses);
        } else {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
        }
    }
    close(sock);
    vTaskDelete(NULL);
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    char resp_str[2048];
    snprintf(resp_str, sizeof(resp_str),
             "<html><body><h1>Mesh Statuses</h1><p>%s</p>"
             "<form method=\"post\"><label>Send Message to Modules:</label><br>"
             "<input type=\"text\" name=\"message\"><input type=\"submit\"></form>"
             "<meta http-equiv=\"refresh\" content=\"10\"></body></html>",
             statuses);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t send_to_mesh(const char *msg, size_t len) {
    // Send to root via UDP (assume root IP is 192.168.4.2, first DHCP lease)
    struct sockaddr_in dest_addr;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock >= 0) {
        dest_addr.sin_addr.s_addr = inet_addr("192.168.4.2");
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(3334);
        sendto(sock, msg, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        close(sock);
        ESP_LOGI(TAG, "Sent message to root: %s", msg);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to create socket to send message to root");
    return ESP_FAIL;
}    
static esp_err_t root_post_handler(httpd_req_t *req) {
    char buf[100];
    size_t len;
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_ERR_HTTPD_RESP_SEND;
    buf[ret] = '\0';
    char *message_start = strstr(buf, "message=");
    if (message_start) {
        message_start += 8;
        char *end = strchr(message_start, '&');
        if (end) *end = '\0';
        len= strlen(message_start);
        for(int i=0; i<len; i++) {
            if (message_start[i] == '+') message_start[i] = ' ';
        }        // Send to root via UDP (assume root IP is 192.168.4.2, first DHCP lease)
        if (send_to_mesh(message_start, len) == ESP_OK) {
            httpd_resp_send(req, "Message sent", HTTPD_RESP_USE_STRLEN);
        }
        return ESP_OK; 
    }
    httpd_resp_send(req, "Message failed", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_get = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
        httpd_uri_t root_post = {.uri = "/", .method = HTTP_POST, .handler = root_post_handler};
        httpd_register_uri_handler(server, &root_get);
        httpd_register_uri_handler(server, &root_post);
    }
    return server;
}

static void wifi_init_softap(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "mesh_ap",
            .ssid_len = strlen("mesh_ap"),
            .channel = 6,
            .password = "password",
            .max_connection = 10,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
    ESP_LOGI(TAG, "SoftAP started: SSID=%s, password=%s, IP=%s", "mesh_ap", "password", "192.168.4.1");
    link_up = true;
}
void web_mesh_init(void) {
    ESP_LOGI(TAG, "Initializing WiFi SoftAP...");
    wifi_init_softap();
    ESP_LOGI(TAG, "Starting UDP server task...");
    xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Starting web server...");
    start_webserver();

}

void mesh_update(bool override, lin_bar_command_t * newValues) {
    char temp[255];
    size_t len;
    if (!link_up) return;
    
    if (override) {
       len = sprintf(temp, "OVR %d %d %d %d %d %d", newValues->values.value0, newValues->values.value1, newValues->values.value2, newValues->values.value3, newValues->values.value4, newValues->values.value5);
    } else {
        len = sprintf(temp, "LIN %d %d %d %d %d %d", newValues->values.value0, newValues->values.value1, newValues->values.value2, newValues->values.value3, newValues->values.value4, newValues->values.value5);
    }
    send_to_mesh(temp, len);
}   