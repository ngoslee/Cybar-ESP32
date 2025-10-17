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
#include "lin_bar.h"
#include "egg.h"
#include "hardware.h"

#define CONFIG_MESH_ROUTE_TABLE_SIZE 10

void message_handler(const char *msg);

static const char *TAG = "mesh_module";
static EventGroupHandle_t s_wifi_event_group;
const int CONNECTED_BIT = BIT0;
static int mesh_layer = -1;
static char statuses[1024] = "";
static uint8_t my_mac[6];
static bool is_running = true;
static esp_netif_t *sta_netif = NULL;
static struct in_addr sta_ip_addr = {0};
static lin_bar_command_t mesh_lin_cmd;


static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "IP event: %d", event_id);
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        sta_ip_addr.s_addr = event->ip_info.ip.addr;
        ESP_LOGI(TAG, "Got IP address: %s GW %s mask %s", ip4addr_ntoa((ip4_addr_t *)&event->ip_info.ip.addr), ip4addr_ntoa((ip4_addr_t *)&event->ip_info.gw.addr), ip4addr_ntoa((ip4_addr_t *)&event->ip_info.netmask.addr));
        ESP_LOGI(TAG, "STA netif: %p, flags: %x", event->esp_netif, esp_netif_get_flags(event->esp_netif));
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "IP address is:" IPSTR, IP2STR(&((ip_event_got_ip_t *)event_data)->ip_info.ip));
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "Lost IP address");
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
static bool is_mesh_connected = false;
#define MESH_TAG "mesh_event"
static mesh_addr_t mesh_parent_addr;
static esp_netif_t *netif_sta = NULL;
static void mesh_connected_indicator(int layer) {
    // Just a stub for now
    hw_led_set_color(0, 32, 0); // Green
}
static void mesh_disconnected_indicator(void) {
    // Just a stub for now
    hw_led_set_color(32, 0, 0); // Red
}

void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};
    static uint16_t last_layer = 0;

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_STOPPED: {
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_CHILD_CONNECTED: {
        mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                 child_connected->aid,
                 MAC2STR(child_connected->mac));
    }
    break;
    case MESH_EVENT_CHILD_DISCONNECTED: {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 child_disconnected->aid,
                 MAC2STR(child_disconnected->mac));
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, mesh_layer);
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, mesh_layer);
    }
    break;
    case MESH_EVENT_NO_PARENT_FOUND: {
        mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 no_parent->scan_times);
    }
    /* TODO handler for the failure */
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        ESP_LOGD(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR", duty:%d",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr), connected->duty);
        last_layer = mesh_layer;
        mesh_connected_indicator(mesh_layer);
        is_mesh_connected = true;
        if (esp_mesh_is_root()) {
            esp_netif_dhcpc_stop(netif_sta);
            esp_netif_dhcpc_start(netif_sta);
        }
        //esp_mesh_comm_p2p_start();
    }
    break;
    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGD(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);
        is_mesh_connected = false;
        mesh_disconnected_indicator();
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
        mesh_layer = layer_change->new_layer;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");
        last_layer = mesh_layer;
        mesh_connected_indicator(mesh_layer);
    }
    break;
    case MESH_EVENT_ROOT_ADDRESS: {
        mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(root_addr->addr));
    }
    break;
    case MESH_EVENT_VOTE_STARTED: {
        mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
        ESP_LOGD(MESH_TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
                 vote_started->attempts,
                 vote_started->reason,
                 MAC2STR(vote_started->rc_addr.addr));
    }
    break;
    case MESH_EVENT_VOTE_STOPPED: {
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
        break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ: {
        mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
        ESP_LOGD(MESH_TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
                 switch_req->reason,
                 MAC2STR( switch_req->rc_addr.addr));
    }
    break;
    case MESH_EVENT_ROOT_SWITCH_ACK: {
        /* new root */
        mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer, MAC2STR(mesh_parent_addr.addr));
    }
    break;
    case MESH_EVENT_TODS_STATE: {
        mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    }
    break;
    case MESH_EVENT_ROOT_FIXED: {
        mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 root_fixed->is_fixed ? "fixed" : "not fixed");
    }
    break;
    case MESH_EVENT_ROOT_ASKED_YIELD: {
        mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
        ESP_LOGD(MESH_TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
                 MAC2STR(root_conflict->addr),
                 root_conflict->rssi,
                 root_conflict->capacity);
    }
    break;
    case MESH_EVENT_CHANNEL_SWITCH: {
        mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
    }
    break;
    case MESH_EVENT_SCAN_DONE: {
        mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 scan_done->number);
    }
    break;
    case MESH_EVENT_NETWORK_STATE: {
        mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 network_state->is_rootless);
    }
    break;
    case MESH_EVENT_STOP_RECONNECTION: {
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    }
    break;
    case MESH_EVENT_FIND_NETWORK: {
        mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
                 find_network->channel, MAC2STR(find_network->router_bssid));
    }
    break;
    case MESH_EVENT_ROUTER_SWITCH: {
        mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
                 router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
    }
    break;
    case MESH_EVENT_PS_PARENT_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
    }
    break;
    case MESH_EVENT_PS_CHILD_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGD(MESH_TAG, "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, "MACSTR", duty:%d", ps_duty->child_connected.aid-1,
                MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
    }
    break;
    default:
        ESP_LOGD(MESH_TAG, "unknown id:%" PRId32 "", event_id);
        break;
    }
}

static void recv_task(void *pvParameters) { 
    mesh_addr_t from;
    mesh_data_t data;
    int flag;
    data.data = (uint8_t *)malloc(201);
    data.size = 200;
    while (is_running) {
        data.size = 200;
        esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        ESP_LOGD(TAG, "**** Node received data");
        if (err == ESP_OK) {
            if (esp_mesh_is_root()) {
                // Collect status from other modules
                char temp[256];
                snprintf(temp, sizeof(temp), "From " MACSTR ": %s<br>", MAC2STR(from.addr), (char *)data.data);
                strncat(statuses, temp, sizeof(statuses) - strlen(statuses) - 1);
                ESP_LOGD(TAG, "Collected status: %s", temp);
 //               udp_send_to_ap();
 //               ESP_LOGD(TAG, "Sent collected statuses to AP");

            } else {
                // Received broadcast (e.g., from PC), log it
                char temp[256];
                data.data[data.size] = 0; // Null-terminate

                ESP_LOGD(TAG, "Received broadcast: %s", data.data);
                message_handler((char *)data.data);

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
    int sent;
    int err;
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
                
                struct sockaddr_in local_addr;
                local_addr.sin_addr.s_addr = sta_ip_addr.s_addr; // Use assigned IP
                local_addr.sin_family = AF_INET;
                local_addr.sin_port = htons(0); // Any port
                if ((err=bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr))) < 0) {
                    ESP_LOGE(TAG, "Failed to bind UDP socket %d", err);                    
                }

                dest_addr.sin_addr.s_addr = inet_addr("192.168.4.1");
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(3333);

                sent = sendto(sock, full_status, strlen(full_status), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                if (sent < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                } else {
                    ESP_LOGD(TAG, "Sent message to AP: %08X port: %d len: %d %s", 
                         dest_addr.sin_addr.s_addr, ntohs(dest_addr.sin_port), sent, full_status);
                }
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
        int i;
    esp_err_t err;
    int send_count = 0;
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
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
            ESP_LOGD(TAG, "**** Node received data from AP");
            rx_buffer[len] = 0;
            // Broadcast message to mesh nodes
            mesh_data_t data;
            data.data = (uint8_t *)rx_buffer;
            data.size = len;
            data.proto = MESH_PROTO_BIN;
            data.tos = MESH_TOS_P2P;

            esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

            for (i = 0; i < route_table_size; i++) {
                err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
                if (err) {
                    ESP_LOGE(TAG,
                            "[ROOT-2-UNICAST:%d][L:%d] to "MACSTR", heap:%" PRId32 "[err:0x%x, proto:%d, tos:%d]",
                            send_count, mesh_layer, 
                            MAC2STR(route_table[i].addr), esp_get_minimum_free_heap_size(),
                            err, data.proto, data.tos);
                } else {
                    ESP_LOGD(TAG,
                            "[ROOT-2-UNICAST:%d][L:%d][rtableSize:%d] to "MACSTR", heap:%" PRId32 "[err:0x%x, proto:%d, tos:%d]",
                            send_count, mesh_layer,
                            esp_mesh_get_routing_table_size(),
                           
                            MAC2STR(route_table[i].addr), esp_get_minimum_free_heap_size(),
                            err, data.proto, data.tos);
                }
                send_count++;
            }
            //handle message
            message_handler((char *)rx_buffer);
        }
    }
    close(sock);
    vTaskDelete(NULL);
}

void mesh_node_init(void) {
    esp_netif_init();
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));
//    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(NULL, NULL));


    ESP_LOGI(TAG, "WiFi initializing...");    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();


    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL, NULL));
    
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
static uint8_t lin_mode = 1;
void message_handler(const char *msg) {
    uint16_t value[6];
    uint8_t mode = 0;
    
    ESP_LOGD(TAG, "Message handler received: %s", msg);
    // Here you can process incoming messages and update statuses or perform actions
    if (strncmp(msg, "LIN ", 4) == 0) {
        mode = 1;
    } else if (strncmp(msg, "OVR ", 4) == 0) {
        mode = 2;
    } else {
        ESP_LOGW(TAG, "Unknown message format");
        return;
    }
    if (sscanf(msg + 4, "%hd %hd %hd %hd %hd %hd", &value[0], &value[1], &value[2], &value[3], &value[4], &value[5]) != 6) {
        ESP_LOGW(TAG, "Failed to parse values");
        return;
    }
    mesh_lin_cmd.values.value0 = value[0] <= 100? value[0]:0;
    mesh_lin_cmd.values.value1 = value[1] <= 100? value[1]:0;
    mesh_lin_cmd.values.value2 = value[2] <= 100? value[2]:0;
    mesh_lin_cmd.values.value3 = value[3] <= 100? value[3]:0;
    mesh_lin_cmd.values.value4 = value[4] <= 100? value[4]:0;
    mesh_lin_cmd.values.value5 = value[5] <= 100? value[5]:0;

    lin_mode = mode;
    egg_msg_handler();
}   

uint8_t mesh_mode_is_lin(void) {
    return lin_mode == 1;
}   

void mesh_get_command(uint8_t * data){
    memcpy(data, mesh_lin_cmd.bytes, 8);
}