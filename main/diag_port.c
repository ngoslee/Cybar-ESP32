
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"
#include "string.h"

#include "diag_port.h"

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
#include "egg.h"
#include "patterns.h"
#include "hardware.h"
#include "system.h"

#define DIAG_PORT_NUM UART_NUM_0
#define TAG  "DIAG_UART"
QueueHandle_t diag_uart_queue = NULL;

#define RX_BUFFER_SIZE 255

static lin_bar_command_t diag_command;
static uint8_t mode_prev;
static uint8_t value_prev;

                        /*handle locally, this mean grab a mode string and pretend it's the truck
                            <value>  //retains last mode
                            or
                            <mode><value>
                            d : ditch
                            m : main
                            b : both
                            o : off
                            value : 0-100
                            */

void diag_process(uint8_t * buffer, size_t len)
{
    uint8_t mode = ' ';
    uint8_t smode = ' ';
    uint8_t lmode = ' ';
    int8_t value = -1;
    bool load = false;
    size_t i;
    uint8_t val;

    for (i=0; i< len; i++) {
        val = buffer[i];
        if (val == 'd') mode = 'd';
        if (val == 'b') mode = 'b';
        if (val == 'm') mode = 'm';
        if (val == 'o') {
            mode = 'o';
            smode = 'o';
        }
        if (val == 'k') smode = 'k';
        if (val == 'w') smode = 'w';
        if (val == 's') smode = 's';

        if (val == 'l') load = true;
        if (val== 'L') lmode = 'L';
        if (val== 'R') lmode = 'R';
        if (val== 'C') lmode = 'C';
        if (val== 'O') lmode = 'O';

        if ((val >= '0') && (val <= '9')) {
            if (value == -1) value = 0;
            value = value *10;
            value += (val - '0');
        }
    }
    if (load) {
        if ((value >= 0) && (value <=7 )) {
            hardawre_load_set_state(HW_LOAD_1, value & 0x01);
            hardawre_load_set_state(HW_LOAD_2, value & 0x02);
            hardawre_load_set_state(HW_LOAD_3, value & 0x04);
        } else {
            ESP_LOGE(TAG, "Load value out of range: %d", value);
        }
        return;
    }
    if (lmode != ' ') {
        system_set_load_mode(lmode);
        if (lmode == 'C') {
            system_set_load_mode(LOAD_MODE_COMBO);
            uart_write_bytes(UART_NUM_0, "Load mode: COMBO\n", 17);
        } else if (lmode == 'L') {
            system_set_load_mode(LOAD_MODE_LEFT);
            uart_write_bytes(UART_NUM_0, "Load mode: LEFT\n", 16);
        } else if (lmode == 'R') {
            system_set_load_mode(LOAD_MODE_RIGHT);
            uart_write_bytes(UART_NUM_0, "Load mode: RIGHT\n", 17);
        } else if (lmode == 'O') {
            system_set_load_mode(LOAD_MODE_OFF);
            uart_write_bytes(UART_NUM_0, "Load mode: OFF\n", 15);
        }
        return;
    }
    if (mode == ' ') {
        mode = mode_prev;
    }
    if ((value > 100) || (value < 0)) value = value_prev;

    mode_prev = mode;
    value_prev = value;
//    ESP_LOGI(TAG, "mode %c value %u\n", mode, value);

    if (smode == 'k') {
        sequenceSelect(SEQ_KITT);
        uart_write_bytes(UART_NUM_0, "KITT MODE\n", 10);

    } else if (smode == 'w') {
        sequenceSelect(SEQ_WIG_WAG);
        uart_write_bytes(UART_NUM_0, "WIG WAG MODE\n", 13);
    } else if (smode == 's') {
        sequenceSelect(SEQ_SWEEP);
        uart_write_bytes(UART_NUM_0, "SWEEP MODE\n", 11);
    }
    else if (smode == 'o') {
        sequenceSelect(SEQ_IDLE);
    }

    lin_bar_command_t msg = {.bytes = {0}};
    if ((mode == 'd') || (mode == 'b')) {
        msg.values.value0 = value;
        msg.values.value5 = value;
    }
    if ((mode == 'm') || (mode == 'b')) {
        msg.values.value1 = value;
        msg.values.value2 = value;
        msg.values.value3 = value;
        msg.values.value4 = value;
    }

    memcpy(diag_command.bytes, msg.bytes, 8);
    egg_msg_handler();
}

void diag_get_command(uint8_t data[8]) {
    memcpy(data, diag_command.bytes, 8);
}

void diag_parse(uint8_t * buffer, size_t *len)
{
    size_t command_start = 0;
    size_t command_end = 0;
 //   ESP_LOGI(TAG, "buffer size: %u last char '%c' %u", *len, buffer[*len -1], buffer[*len -1]);
    for (command_end = command_start; command_end < *len; command_end++) {
        if (buffer[command_end] == '\r') {
            diag_process(buffer + command_start, command_end - command_start);
            command_start = command_end + 1;
        }
    }

    size_t i;
    size_t count = 0;
    if (command_start != 0) {
        for (i = command_start; i < *len; i++) {
            buffer[count] = buffer[i];
            count ++;
        }
        *len = count;
    }
}

void uart_task(void *pvParameters)
{
    uart_event_t event;
    static uint8_t rx_buffer[RX_BUFFER_SIZE + 1];
    static size_t rx_buffer_count = 0;
    uint8_t * temp;

    for (;;) {
        //Waiting for UART event.
        if (xQueueReceive(diag_uart_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
            //Event of UART receiving data
            case UART_DATA:
                if (event.size) {
                    /* //send diag to BT
                    if (spp_is_connected()) {
                        temp = (uint8_t *)malloc(sizeof(uint8_t)*event.size);
                        if (temp == NULL) {
                            ESP_LOGE(TAG, "%s malloc.1 failed", __func__);
                            break;
                        }
                        uart_read_bytes(DIAG_PORT_NUM, temp, event.size, portMAX_DELAY);
                        spp_send(temp, event.size);
                        free(temp);
                        rx_buffer_count = 0;
                    } else */ {

                        if ((event.size + rx_buffer_count) < RX_BUFFER_SIZE) {
                            uart_read_bytes(DIAG_PORT_NUM, rx_buffer + rx_buffer_count, event.size, portMAX_DELAY);
                            rx_buffer_count += event.size;
                        } else {
                            uart_read_bytes(DIAG_PORT_NUM, rx_buffer, event.size, portMAX_DELAY);
                            rx_buffer_count = event.size;
                        }
                        diag_parse(rx_buffer, &rx_buffer_count);

                    }
                }
                break;
            default:
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

void diag_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_RTS,
        .rx_flow_ctrl_thresh = 124,
        .source_clk = UART_SCLK_DEFAULT,
    };

    //Install UART driver, and get the queue.
    uart_driver_install(DIAG_PORT_NUM, 4096, 8192, 10, &diag_uart_queue,0);
    //Set UART parameters
    uart_param_config(DIAG_PORT_NUM, &uart_config);
    //Set UART pins
    uart_set_pin(DIAG_PORT_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreate(uart_task, "uTask", 4096, (void*)DIAG_PORT_NUM, 8, NULL);
}

void diag_port_write(void * data_ptr, size_t len)
{
    uart_write_bytes(DIAG_PORT_NUM, data_ptr, len);
}

