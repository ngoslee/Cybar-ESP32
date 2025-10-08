#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"
#include "string.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "ble_spp_server.h"
#include "esp_gatt_common_api.h"
#include "esp_timer.h"
#include "lin_bar.h"
#include "lin_truck.h"
#include "user.h"
#include "patterns.h"
#include "diag_port.h"
#include "hardware.h"
#include "web_server.h"
#include "system.h"
#define TAG "MAIN"



void app_main(void)
{
    esp_err_t ret;
//    sequenceSelect
    sequenceSelect(SEQ_IDLE);

    hardware_init();
    hw_led_init();
    system_init();
 //   spp_task_init();
//    ESP_LOGI(TAG, "SPP task started" );
    bar_lin_init();
    ESP_LOGI(TAG, "Bar task started" );
    truck_lin_init();
    ESP_LOGI(TAG, "Truck task started" );
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    web_server_init();
 //   spp_init();
    diag_uart_init();

    while (1) {
    hw_toggle_led();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}