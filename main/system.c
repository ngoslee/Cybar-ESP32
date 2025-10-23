#include "system.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_mac.h"

static system_load_mode_enum_t load_mode = LOAD_MODE_OFF;
static system_lin_mode_enum_t lin_mode = LIN_MODE_MIM;
static system_node_type_enum_t node_type = NODE_TYPE_WEB;
static const char *TAG = "SYS";
static char system_name[16] = "Lightbar";
typedef struct {
    const char mac[18];
    const char name[16];
    system_load_mode_enum_t load_mode;
    system_lin_mode_enum_t lin_mode;
    system_node_type_enum_t node_type;
} system_module_t;
#define MODULES 4
static const system_module_t module[MODULES] = {
    {"38:18:2B:F1:02:3C", "Charlie", LOAD_MODE_COMBO, LIN_MODE_MIM, NODE_TYPE_WEB}, //truck and user interface
    {"38:18:2B:F1:E7:C4", "Larry", LOAD_MODE_LEFT, LIN_MODE_LISTEN, NODE_TYPE_MODULE}, //left side lights
    {"38:18:2B:F0:A7:08", "Rick", LOAD_MODE_RIGHT, LIN_MODE_LISTEN, NODE_TYPE_MODULE}, //right side light
    {"38:18:2B:F2:25:24", "Ghost", LOAD_MODE_COMBO, LIN_MODE_LISTEN, NODE_TYPE_MODULE}, //isolated lightbar 
 
};

void system_set_load_mode(system_load_mode_enum_t mode)
{
    // Just a stub for now
    if (mode >= LOAD_MODE_NUM) mode = LOAD_MODE_OFF;
    load_mode = mode;
}

system_load_mode_enum_t system_get_load_mode(void)
{
    return load_mode;
}

void system_set_lin_mode(system_lin_mode_enum_t mode)
{
    // Just a stub for now
    if (mode >= LIN_MODE_NUM) mode = LIN_MODE_MIM;
    lin_mode = mode;
}
system_lin_mode_enum_t system_get_lin_mode(void)
{
    return lin_mode;
}
char * system_get_name(void)
{
    return system_name;
}
system_node_type_enum_t system_get_node_type(void)
{
    return node_type;
}
void system_set_node_type(system_node_type_enum_t type)
{
    // Just a stub for now
    if (type >= NODE_TYPE_NUM) type = NODE_TYPE_WEB;
    node_type = type;
}

void system_init(void)
{
    char mac_str[18];
    int i;
    load_mode = LOAD_MODE_OFF;
    lin_mode = LIN_MODE_MIM;
    node_type = NODE_TYPE_WEB;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    bool known = false;
    for (i=0; i< MODULES; i++) {
        if (strcmp(mac_str, module[i].mac) == 0) {
            system_set_load_mode(module[i].load_mode);
            system_set_lin_mode(module[i].lin_mode);
            system_set_node_type(module[i].node_type);
            strncpy(system_name, module[i].name, sizeof(system_name)-1);
            system_name[sizeof(system_name)-1] = 0;
            ESP_LOGI("SYS", "Module %s, Load mode: %d, LIN mode: %d Node type: %d", module[i].name, module[i].load_mode, module[i].lin_mode, module[i].node_type);
            known = true;
            break;
        }
    }
    if (!known) {
        ESP_LOGI("SYS", "Unknown MAC address: %s", mac_str);
    } 
}