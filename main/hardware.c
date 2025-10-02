#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "hardware.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "driver/rmt.h"
#include "pinout.h"
#include "esp_adc/adc_continuous.h"

#define TAG "HW"

#define HW_LOAD_POLL_PERIOD (50) //ms
#define HW_ADC_CHANNEL_COUNT 5
#define BLINK_GPIO (16) //CONFIG_BLINK_GPIO

static uint8_t s_led_state = 0;

static led_strip_handle_t led_strip;
static void hw_load_task(void *arg);
void adc_init(void);

void hardawre_load_set_state(hardware_load_enum_t load, bool state)
{
    switch (load) {
        case HW_LOAD_1:
            gpio_set_level(HW_LOAD_1_PIN, state);
            break;
        case HW_LOAD_2:
            gpio_set_level(HW_LOAD_2_PIN, state);
            break;
        case HW_LOAD_3:
            gpio_set_level(HW_LOAD_3_PIN, state);
            break;
        default:
            ESP_LOGE(TAG, "unknown load %d", load);

    }
}

void hardawre_load_set_states(uint8_t value) {
        if ((value >= 0) && (value <=3 )) {
            hardawre_load_set_state(HW_LOAD_1, value & 0x01);
            hardawre_load_set_state(HW_LOAD_2, value & 0x02);
            hardawre_load_set_state(HW_LOAD_3, value & 0x04);
        } else {
            ESP_LOGE(TAG, "Load value out of range: %d", value);
        }
}

void hardware_init(void) {
    gpio_set_level(HW_LOAD_1_PIN, 0);
    gpio_set_direction(HW_LOAD_1_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(HW_LOAD_2_PIN, 0);
    gpio_set_direction(HW_LOAD_2_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(HW_LOAD_3_PIN, 0);
    gpio_set_direction(HW_LOAD_3_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(HW_LOAD_SENSE_EN, 1);
    gpio_set_direction(HW_LOAD_SENSE_EN, GPIO_MODE_OUTPUT);

    adc_init();

    xTaskCreate(hw_load_task, "load_task", 4096, NULL, 11, NULL);

}

void hw_led_init(void) {
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);

}

void hw_toggle_led(void)
{
    /* Toggle the LED state */
    s_led_state++;
    if (s_led_state > 3) s_led_state = 0;

    /* If the addressable LED is enabled */
    if (s_led_state) {
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        led_strip_set_pixel(led_strip, 0, s_led_state==1?16:0, s_led_state==2?16:0, s_led_state==3?16:0);
        /* Refresh the strip to send data */
        led_strip_refresh(led_strip);
    } else {
        /* Set all LED off to clear all pixels */
        led_strip_clear(led_strip);
    }
}

void hw_lin_enable(void)
{
    gpio_set_level(HW_LIN_EN_PIN, 1);
    gpio_set_direction(HW_LIN_EN_PIN, GPIO_MODE_OUTPUT);
}



// Periodic load task
typedef struct {
    adc_channel_t channel;
    uint32_t sampleTotal;
    uint32_t sampleCount;
    uint16_t average;
    bool cmd;
    bool fault;
} load_t;
#define HW_NUM_LOADS 3
load_t loads[HW_NUM_LOADS] = {{.channel = HW_LOAD_1_SENSE_CH},
                {.channel = HW_LOAD_2_SENSE_CH},
                {.channel = HW_LOAD_3_SENSE_CH},
            };

static adc_channel_t channel[HW_ADC_CHANNEL_COUNT] = {
    HW_LOAD_1_SENSE_CH, 
    HW_LOAD_2_SENSE_CH, 
    HW_LOAD_3_SENSE_CH,
    HW_BATT_SENSE_CH,
    HW_TEMP_SENSE_CH
};

typedef struct {
    uint32_t sampleTotal;
    uint32_t sampleCount;
    uint16_t average;
} adc_result_t;
#define OVERSAMPLE 16
#define TIMES (HW_ADC_CHANNEL_COUNT * OVERSAMPLE * 2)
#define GET_UNIT(x)        ((x>>3) & 0x1)
adc_continuous_handle_t adc_handle;

adc_result_t adc_results[8]; 

static void hw_load_task(void *arg) {

    uint16_t print_delay= 0;
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[TIMES] = {0};
    memset(result, 0xcc, TIMES);

    //inital setup:
  //  adc_digi_start();
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI("HW", "HW Task started");
    adc_continuous_start(adc_handle);
    while (1) {
        //read ADCs
        ret = adc_continuous_read(adc_handle, result, TIMES, &ret_num, 1000);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            if (ret == ESP_ERR_INVALID_STATE) {
                //TODO: handle this
                ESP_LOGW(TAG, "Data overrun");
            }
            print_delay ++ ;
            if (print_delay > 200) print_delay = 0;
            if (!print_delay) ESP_LOGI("TASK:", "ret is %x, ret_num is %d", ret, ret_num);
            #if 1

            for (int i = 0; i < ret_num; i += 2) {
                adc_digi_output_data_t *p = (void*)&result[i];
                if ((p->type1.channel >= 0) && (p->type1.channel < 8)) {
                    adc_results[p->type1.channel].sampleTotal += p->type1.data;
                    adc_results[p->type1.channel].sampleCount ++;
                }
    //            ESP_LOGI(TAG, "Unit: %d, Channel: %d, Value: %x", 1, p->type1.channel, p->type1.data);   
            }
 

            if (!print_delay) {
                uint32_t cnt = adc_results[0].sampleCount;
                for (int i=0; i< 8; i++) {
                    if (adc_results[i].sampleCount > 0) {
                       adc_results[i].average = adc_results[i].sampleTotal / adc_results[i].sampleCount;
                    } else {
                         adc_results[i].average = 0;
                    }
                    adc_results[i].sampleCount = 0;
                    adc_results[i].sampleTotal = 0;
                }
                ESP_LOGI(TAG, "cnt %d batt %d   Temp %d loads %d %d %d", cnt, 
                    adc_results[5].average,
                    adc_results[0].average,
                    adc_results[4].average,
                    adc_results[7].average,
                    adc_results[6].average);
            }
            #endif
            vTaskDelay(1);
        } else if (ret == ESP_ERR_TIMEOUT) {
            /**
             * ``ESP_ERR_TIMEOUT``: If ADC conversion is not finished until Timeout, you'll get this return error.
             * Here we set Timeout ``portMAX_DELAY``, so you'll never reach this branch.
             */
            ESP_LOGW(TAG, "No data, increase timeout or reduce conv_num_each_intr");
            vTaskDelay(1);
        }

    }

 //   adc_digi_stop();
 //   ret = adc_digi_deinitialize();
 #if 0
    assert(ret == ESP_OK);

        batt_adc = 0;
        temp_adc = 0;        
        for (j=0; j < HW_NUM_LOADS; j++) {
            loads[j].sampleTotal = 0;
            loads[j].sampleCount = 0;
        }
        for (i =0; i< 16; i++) {
           batt_adc += adc1_get_raw(HW_BATT_SENSE_CH);
            temp_adc += adc1_get_raw(HW_TEMP_SENSE_CH);
            for (j=0; j < HW_NUM_LOADS; j++) {
                loads[j].sampleTotal += adc1_get_raw(loads[j].channel);
                loads[j].sampleCount ++;
            }
        }

        ESP_LOGI(TAG, "batt %d   Temp %d loads %d %d %d", batt_adc>>4, temp_adc>>4, loads[0].sampleTotal/ loads[0].sampleCount, 
            loads[1].sampleTotal/ loads[1].sampleCount, 
            loads[2].sampleTotal/ loads[2].sampleCount);
    
//        vTaskDelay(pdMS_TO_TICKS(HW_LOAD_POLL_PERIOD));
        vTaskDelay(pdMS_TO_TICKS(1000));
        #endif

    
}

void adc_init(void) {

    adc_continuous_handle_cfg_t adc_handle_cfg= {
        .max_store_buf_size = HW_ADC_CHANNEL_COUNT*OVERSAMPLE*2*4,
        .conv_frame_size = HW_ADC_CHANNEL_COUNT*OVERSAMPLE*2,

    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_handle_cfg, &adc_handle));

    adc_continuous_config_t adc_cfg = {
    //    .conv_limit_en = 1,
    //    .conv_limit_num = 250,
        .sample_freq_hz = HW_ADC_CHANNEL_COUNT * OVERSAMPLE * 250,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    };

    adc_digi_pattern_config_t adc_pattern[HW_ADC_CHANNEL_COUNT] = {0};
    adc_cfg.pattern_num = HW_ADC_CHANNEL_COUNT;
    for (int i = 0; i < HW_ADC_CHANNEL_COUNT; i++) {
        uint8_t ch = channel[i] & 0x7;
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = ch;
        adc_pattern[i].unit = ADC_UNIT_1;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

        ESP_LOGI(TAG, "adc_pattern[%d].atten is :%x", i, adc_pattern[i].atten);
        ESP_LOGI(TAG, "adc_pattern[%d].channel is :%x", i, adc_pattern[i].channel);
        ESP_LOGI(TAG, "adc_pattern[%d].unit is :%x", i, adc_pattern[i].unit);
    }
    adc_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &adc_cfg));


}