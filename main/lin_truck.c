#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/uart_hal.h"
#include "lin_bar.h"
#include "soc/uart_struct.h"
#include "ble_spp_server.h"
#include "lin.h"
#include "user.h"
#include "egg.h"

#define TRUCK_LIN_UART_PORT UART_NUM_2
#define TRUCK_LIN_TX_PIN GPIO_NUM_22
#define TRUCK_LIN_RX_PIN GPIO_NUM_23
#define LIN_BAUD_RATE 19200

#define UART_BUF_SIZE 256

static const char *TAG = "LIN_TRUCK";
//00 00 60 9D 01
static uint8_t lin_txDataShadow[8 + 1] = {0, 0, 0x60, 0x9D, 0x01, 0, 0, 0, 0};
static volatile uint8_t lin_txData[8 + 1] = {0, 0, 0x60, 0x9D, 0x01, 0, 0, 0, 0};

static volatile uint8_t lin_txDataEdit = 0;

/*read data is the 0x0A packet
respode to 0x0B message*/
QueueHandle_t truck_uart_queue = NULL;

static lin_port_t truck_lin_port = { .uart = TRUCK_LIN_UART_PORT,
                                    .tx_pin = TRUCK_LIN_TX_PIN,
                                    .rx_pin = TRUCK_LIN_RX_PIN,
                                };

static uint8_t rx_data_shadow[TRUCK_TO_BAR_DATA_LEN]; //max length
static lin_msg_t truck_to_bar_msg = {
        .id = TRUCK_TO_BAR_ID,
        .len = TRUCK_TO_BAR_DATA_LEN,
        .data = rx_data_shadow,
    };

static uint8_t tx_data_shadow[8+1];
static lin_msg_t bar_to_truck_msg = {
        .id = BAR_TO_TRUCK_ID,
        .len = BAR_TO_TRUCK_DATA_LEN,
        .data = tx_data_shadow,
    };  

typedef enum {
    LIN_STATE_WAIT_BREAK, 
    LIN_STATE_WAIT_BREAK_SIGNAL ,
    LIN_STATE_WAIT_BREAK_BYTE ,
    LIN_STATE_WAIT_SYNC ,
    LIN_STATE_WAIT_ID ,
    LIN_STATE_WAIT_DATA ,
    LIN_STATE_WAIT_CHECKSUM ,
    LIN_STATE_WAIT_ECHO,
    LIN_STATE_NUM ,
} linStateEnum_t;

static uint8_t data_prev[BAR_TO_TRUCK_DATA_LEN] = {0};

void truck_lin_set_bar_data_response(uint8_t * data)
{
    lin_txDataEdit = 1;
    memcpy(lin_txData, data, BAR_TO_TRUCK_DATA_LEN);
    if (memcmp(data_prev, data, BAR_TO_TRUCK_DATA_LEN)) {
    //    ESP_LOGI(TAG, "Bar resp: %02X %02X %02X %02X %02X", data[0], data[1], data[2], data[3], data[4]);
        memcpy(data_prev, data, BAR_TO_TRUCK_DATA_LEN);
    }
    lin_txDataEdit = 0;
}

void truck_lin_task(void * arg)
{
    bool lin_debug = false;
    static linStateEnum_t state = LIN_STATE_WAIT_BREAK;
    uint8_t rxBuffer[256];
    uint8_t rxBufferCount = 0;
    static uint8_t lin_pid;
    static uint8_t lin_data[8];

    static uint8_t lin_data_count;
    static uint8_t lin_checksum;
    static uint8_t rxByteCount;
    static uint8_t txByteCount;
    static uint8_t txEchoCount;
    char temp_str[255];
    static bool sniff = false;
    #define SNIFF_MAX_SIZE 16
    static uint8_t sniff_buf[SNIFF_MAX_SIZE];
    static uint8_t sniff_len=0;
    uint8_t temp_len;
    uint8_t tmp_len;
    uint8_t newByte;
    uart_event_t event;
    uint8_t i;
    ESP_LOGI(TAG,"truck lin task running");

    for (;;) {
        //Waiting for UART event.
        #if 0
        if (xQueueReceive(truck_uart_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            rxBufferCount = 0;
            switch (event.type) {
            //Event of UART receiving data
            case UART_DATA:
                if (event.size) {
                    uart_read_bytes(TRUCK_LIN_UART_PORT, rxBuffer, event.size, portMAX_DELAY);
                    rxBufferCount = event.size;                   
         //           ESP_LOGI(TAG,"Data count %d %02X %02X %02X %02X", event.size, rxBuffer[0], rxBuffer[1], rxBuffer[2], rxBuffer[3]);
                }
                break;
            case UART_BREAK:
            case UART_FRAME_ERR:
           //     ESP_LOGI(TAG,"HW Break");
    //            uart_write_bytes(UART_NUM_0, "\nB ", 3);
      //          state = LIN_STATE_WAIT_SYNC;
#if 0
                if (state == LIN_STATE_WAIT_BREAK_SIGNAL) {
                    state = LIN_STATE_WAIT_SYNC;
                } else {
                    state = LIN_STATE_WAIT_BREAK_BYTE;
                }
                rxBufferCount = 0;
            #endif
                break;

            default:
                break;
            }
                #endif
            {
            rxBufferCount = uart_read_bytes(truck_lin_port.uart, rxBuffer, 1, 20);
            for (i=0; i< rxBufferCount; i++) {
                newByte = rxBuffer[i];            


                if (sniff) {
                    if ((state == LIN_STATE_WAIT_BREAK) || ((state == LIN_STATE_WAIT_BREAK_SIGNAL) || (state == LIN_STATE_WAIT_BREAK_BYTE))) {
                        sniff_buf[sniff_len] = newByte;
                        if (sniff_len < SNIFF_MAX_SIZE) sniff_len ++;
                    } else {
                        if (sniff_len > 1) {
                            temp_len = sprintf(temp_str,  "Msg id %02X len: %d, %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", sniff_buf[0], sniff_len-3, sniff_buf[1],sniff_buf[2],sniff_buf[3],sniff_buf[4],sniff_buf[5],sniff_buf[6],sniff_buf[7],sniff_buf[8],sniff_buf[9] );
                            ESP_LOGI(TAG, "%s", temp_str);
                         //   spp_send((uint8_t *)temp_str, temp_len);
                            sniff_len = 0;
                            sniff = false;
                        }
                    }

                }
                switch (state) {
                    case LIN_STATE_WAIT_BREAK:
                        if (newByte == 0x00) {
                            if (lin_debug)    uart_write_bytes(UART_NUM_0, "\nb ", 3);
                            state = LIN_STATE_WAIT_SYNC;
                        }
#if 0
                        if (newByte == 0x00) {
                            state = LIN_STATE_WAIT_BREAK_SIGNAL;
                        }
                        break;

                    case LIN_STATE_WAIT_BREAK_SIGNAL:
                        if (newByte != 0x00) {
                            state = LIN_STATE_WAIT_BREAK;
                        }
                        break;


                    case LIN_STATE_WAIT_BREAK_BYTE:
                        if (newByte == 0x00) {
                            state = LIN_STATE_WAIT_SYNC;
                        } else {
                            state = LIN_STATE_WAIT_BREAK;
                        }
#endif
                        break;
                    case LIN_STATE_WAIT_SYNC:
                        if (newByte == 0x55) {
//                            uart_write_bytes(UART_NUM_0, "Sync ", 5);
                            if (lin_debug) uart_write_bytes(UART_NUM_0, "S ", 2);
                            state = LIN_STATE_WAIT_ID;
                        } else if (newByte == 0x00) {
                            if (lin_debug) uart_write_bytes(UART_NUM_0, "\nb ", 3);                            
//                            state = LIN_STATE_WAIT_BREAK_SIGNAL;
                            state = LIN_STATE_WAIT_SYNC;
                        } else {
                            state = LIN_STATE_WAIT_BREAK;
                        }
                        break;

                    case LIN_STATE_WAIT_ID:
                        lin_pid = newByte;
                        if (lin_pid !=  lin_calc_pid(lin_pid) ) {
                            ESP_LOGE(TAG, "ID error %02X", lin_pid & 0x3F);
                            state = LIN_STATE_WAIT_BREAK;
                            break;
                        }
                        if (lin_debug) uart_write_bytes(UART_NUM_0, "ID ", 3);

//                        temp_len = sprintf(temp_str, " %02X", lin_pid & 0x3F);
    //                    uart_write_bytes(UART_NUM_0, (uint8_t *)temp_str, temp_len);

             //           ESP_LOGI(TAG, "got ID %02x", lin_pid & 0x3f);
                        switch (lin_pid & 0x3F) {
                            case TRUCK_TO_BAR_ID:
                               rxByteCount = TRUCK_TO_BAR_DATA_LEN;
                               lin_data_count = 0;
                               if (lin_debug)  uart_write_bytes(UART_NUM_0, "Rx ", 3);

                               state = LIN_STATE_WAIT_DATA;
                               break;
                            case BAR_TO_TRUCK_ID:
                        //        ESP_LOGI(TAG, "ID at %lld", esp_timer_get_time());
                                txByteCount = BAR_TO_TRUCK_DATA_LEN;
                                //get data 
                                if (lin_txDataEdit == 0)
                                {
                                    memcpy(lin_txDataShadow, lin_txData, BAR_TO_TRUCK_DATA_LEN);
                                }
                                lin_checksum = lin_calc_checksum(lin_pid, lin_txDataShadow, txByteCount);
                                lin_txDataShadow[txByteCount] = lin_checksum;
                                txEchoCount = txByteCount + 1;
                                if (lin_debug) uart_write_bytes(UART_NUM_0, "Tx ", 3);

                                uart_write_bytes(TRUCK_LIN_UART_PORT, lin_txDataShadow, txEchoCount);     
                       //         ESP_LOGI(TAG, "Written by at %lld", esp_timer_get_time());
                       //         ESP_LOGI(TAG, "Checksum is %02X",lin_checksum);    
                                state = LIN_STATE_WAIT_ECHO;  
                                break;
                            default:
                                ESP_LOGE(TAG, "Unknown ID %02X", lin_pid & 0x3F);
                                //temp_len = sprintf(temp_str,"Truck ID: %02X\n", lin_pid & 0x3F);
                              //  spp_send((uint8_t *)temp_str, temp_len);
                                sniff = true;
                                sniff_buf[0]  = lin_pid;
                                sniff_len=1;

                                state = LIN_STATE_WAIT_BREAK;
                                break;
                        }
                        break;

                    case LIN_STATE_WAIT_DATA:
                        temp_len = sprintf(temp_str," %02X", newByte);
      //                  uart_write_bytes(UART_NUM_0, (uint8_t *)temp_str, temp_len);
                        if (lin_debug) uart_write_bytes(UART_NUM_0, "<", 1);

                        lin_data[lin_data_count++] = newByte;
                        if (lin_data_count == rxByteCount) {
                            state = LIN_STATE_WAIT_CHECKSUM;
                        }
                        break;

                    case LIN_STATE_WAIT_CHECKSUM:
                        if (newByte != lin_calc_checksum(lin_pid, lin_data, rxByteCount)) {
                            ESP_LOGE(TAG, "Checksum error"); 
                            state = LIN_STATE_WAIT_BREAK;
                            break;
                        } else {
                            //handle received data
                            if (lin_debug) uart_write_bytes(UART_NUM_0, "cs ", 3);
//                            ESP_LOGI(TAG, "Packet recevied"); 
                            egg_msg_handler(lin_data, rxByteCount); 
                        }

                        state = LIN_STATE_WAIT_BREAK;
                        break;

                    case LIN_STATE_WAIT_ECHO:
                        if (lin_debug) uart_write_bytes(UART_NUM_0, ">", 1);

                        txEchoCount--;
                        if (txEchoCount == 0) {
                            //ESP_LOGI(TAG, "Packet sent"); 
                            state = LIN_STATE_WAIT_BREAK;
                        }
                        break;

                    default:
                        break;
                } 
                if (lin_debug) temp_len = sprintf(temp_str,"%02X ", newByte);
                if (lin_debug) uart_write_bytes(UART_NUM_0, (uint8_t *)temp_str, temp_len);

            }
        }
    }
    vTaskDelete(NULL);
}



// Initialize UART and LIN
void truck_lin_init(void) {
  //  lin_init(&truck_lin_port);
    uart_config_t uart_config = {
        .baud_rate = LIN_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
     uart_intr_config_t uart_intr = {
      .intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M
                          | UART_RXFIFO_TOUT_INT_ENA_M
                          | UART_FRM_ERR_INT_ENA_M
                          | UART_RXFIFO_OVF_INT_ENA_M
                          | UART_BRK_DET_INT_ENA_M
                          | UART_PARITY_ERR_INT_ENA_M,
      .rxfifo_full_thresh = 1,
      .rx_timeout_thresh = 10,
      .txfifo_empty_intr_thresh = 10
    };
    ESP_ERROR_CHECK(uart_intr_config(truck_lin_port.uart, &uart_intr));
    ESP_ERROR_CHECK(uart_driver_install(truck_lin_port.uart, UART_BUF_SIZE, UART_BUF_SIZE, 16, NULL, ESP_INTR_FLAG_LEVEL2));
    ESP_LOGI(TAG, "Driver installed"); 
    ESP_ERROR_CHECK(uart_param_config(truck_lin_port.uart, &uart_config));
    ESP_ERROR_CHECK(gpio_set_pull_mode(truck_lin_port.rx_pin, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_pull_mode(truck_lin_port.tx_pin, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(uart_set_rx_full_threshold(truck_lin_port.uart, 1)); // Trigger interrupt after 1 byte
    ESP_LOGI(TAG, "Params configured"); 
    ESP_ERROR_CHECK(uart_set_pin(truck_lin_port.uart, truck_lin_port.tx_pin, truck_lin_port.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "Pins set"); 

    xTaskCreate(truck_lin_task, "truck_lin_task", 4096, NULL, 11, NULL);
    ESP_LOGI(TAG, "Task created"); 
}