#include "mesh_node.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_mac.h" // For MACSTR and MAC2STR macros


static const char *TAG = "mesh_module";
static EventGroupHandle_t s_wifi_event_group;
const int CONNECTED_BIT = BIT0;
static int mesh_layer = -1;
static char statuses[1024] = "";
static uint8_t my_mac[6];
static bool is_running = true;
static esp_netif_t *sta_netif = NULL;

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "IP event: %d", event_id);
    if (event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "IP address is:" IPSTR, IP2STR(&((ip_event_got_ip_t *)event_data)->ip_info.ip));
    }
}


static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WiFi event: %d", event_id);
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "*** station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
  //      esp_wifi_set_config(WIFI_IF_STA, NULL); // No config change, just ensure interface is up
  //      esp_netif_dhcpc_start(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "*** station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        }
}

static void mesh_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "Mesh event: %d", event_id);
    mesh_event_info_t *event = (mesh_event_info_t *)event_data;
    if (event_id == MESH_EVENT_PARENT_CONNECTED) {
        mesh_layer = event->connected.self_layer;
        ESP_LOGI(TAG, "Connected to AP, layer: %d", mesh_layer);
        if (esp_mesh_is_root()) {
            ESP_LOGI(TAG, "Node is root, enabling DHCP client");
            if (sta_netif) {

                esp_netif_dhcpc_stop(sta_netif);
                esp_netif_dhcpc_start(sta_netif);
            } else {
                ESP_LOGW(TAG, "No STA netif found for DHCP client");
            }
        }
    } else if (event_id == MESH_EVENT_LAYER_CHANGE) {
        mesh_layer = event->layer_change.new_layer;
        ESP_LOGI(TAG, "Layer changed: %d", mesh_layer);
    } else if (event_id == MESH_EVENT_ROOT_ADDRESS) {
        // Node is now the root, enable DHCP client
        ESP_LOGI(TAG, "Node is root, enabling DHCP client");
    }
}

static void recv_task(void *pvParameters) {
    mesh_addr_t from;
    mesh_data_t data;
    int flag;
    data.data = (uint8_t *)malloc(200);
    data.size = 200;
    while (is_running) {
        data.size = 200;
        esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        ESP_LOGI(TAG, "**** Node received data");
        if (err == ESP_OK) {
            if (esp_mesh_is_root()) {
                // Collect status from other modules
                char temp[256];
                snprintf(temp, sizeof(temp), "From " MACSTR ": %s<br>", MAC2STR(from.addr), (char *)data.data);
                strncat(statuses, temp, sizeof(statuses) - strlen(statuses) - 1);
                ESP_LOGI(TAG, "Collected status: %s", temp);
            } else {
                // Received broadcast (e.g., from PC), log it
                ESP_LOGI(TAG, "Received broadcast: %s", (char *)data.data);
            }
        } else {
            ESP_LOGE(TAG, "mesh_recv failed: %d %s",err, esp_err_to_name(err));
        }
    }
    free(data.data);
    vTaskDelete(NULL);
}

static void send_task(void *pvParameters) {
    int count = 0;
    mesh_data_t data;
    data.data = (uint8_t *)malloc(200);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    while (is_running) {
        data.size = snprintf((char *)data.data, 200, "Status count: %d", count++);
        if (esp_mesh_is_root()) {
            // Wait for IP if needed
            xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
            // Prepend own status and send collected statuses to AP via UDP
            char full_status[1024];
            snprintf(full_status, sizeof(full_status), "Root "MACSTR": Status count: %d<br>%.900s", MAC2STR(my_mac), count - 1, statuses);
            struct sockaddr_in dest_addr;
            int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            if (sock >= 0) {
                dest_addr.sin_addr.s_addr = inet_addr("192.168.4.1");
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(3333);
                sendto(sock, full_status, strlen(full_status), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                close(sock);
            }
            // Clear statuses after sending
            statuses[0] = '\0';
        } else {
            // Send status to root
            esp_mesh_send(NULL, &data, MESH_DATA_TODS, NULL, 0);
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Send every 5 seconds
    }
    free(data.data);
    vTaskDelete(NULL);
}

static void udp_recv_from_ap_task(void *pvParameters) {
   // if (!esp_mesh_is_root())  vTaskDelete(NULL);
    xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "**** Node connected to AP");
    struct sockaddr_in dest_addr;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(3334);
    bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    while (is_running) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        char rx_buffer[128];
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &addr_len);
        if (len > 0) {
            ESP_LOGI(TAG, "**** Node received data from AP");
            rx_buffer[len] = 0;
            // Broadcast message to mesh nodes
            mesh_data_t data;
            data.data = (uint8_t *)rx_buffer;
            data.size = len;
            data.proto = MESH_PROTO_BIN;
            data.tos = MESH_TOS_DEF;
            mesh_addr_t broadcast_addr = {
                //38:18:2b:f0:a7:08:
                 .addr = {0x38, 0x18, 0x2b, 0xF0, 0xa7, 0x08} // Broadcast MAC address
            };
            if (esp_mesh_send(&broadcast_addr, &data, MESH_DATA_FROMDS, NULL, 0)) {
                ESP_LOGE(TAG, "Failed to broadcast message");
            } else {
                ESP_LOGI(TAG, "Broadcasted message to mesh nodes: %s", rx_buffer);
            }
        }
    }
    close(sock);
    vTaskDelete(NULL);
}

void mesh_node_init(void) {
    esp_netif_init();
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&sta_netif, NULL));





    ESP_LOGI(TAG, "WiFi initializing...");    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();


    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_wifi_start());

    mesh_cfg_t mesh_cfg = MESH_INIT_CONFIG_DEFAULT();
    // Mesh ID (6 bytes, arbitrary)
    memcpy((uint8_t *)&mesh_cfg.mesh_id, "MESHID", 6);
    mesh_cfg.channel = 6; // Match AP channel
    mesh_cfg.mesh_ap.max_connection = 6;
    memcpy((uint8_t *)&mesh_cfg.mesh_ap.password, "mesh_pass", strlen("mesh_pass"));
    // Router config (the main AP)
    mesh_cfg.router.ssid_len = strlen("mesh_ap");
    memcpy((uint8_t *)&mesh_cfg.router.ssid, "mesh_ap", mesh_cfg.router.ssid_len);
    memcpy((uint8_t *)&mesh_cfg.router.password, "password", strlen("password"));
    ESP_LOGI(TAG, "Mesh initializing...");
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_instance_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_cfg));
    ESP_LOGI(TAG, "Mesh Starting...");
    ESP_ERROR_CHECK(esp_mesh_start());
    ESP_LOGI(TAG, "Getting MAC...");

    esp_wifi_get_mac(WIFI_IF_STA, my_mac);
    ESP_LOGI(TAG, "My MAC: " MACSTR, MAC2STR(my_mac));
    xTaskCreate(recv_task, "mesh_recv", 4096, NULL, 5, NULL);
    xTaskCreate(send_task, "mesh_send", 4096, NULL, 5, NULL);
    xTaskCreate(udp_recv_from_ap_task, "udp_recv", 4096, NULL, 5, NULL);
}