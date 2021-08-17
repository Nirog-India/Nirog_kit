#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_log.h"

#include "system_routine.h"


#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   64          //Multisampling





static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel = ADC_CHANNEL_6;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

uint32_t raw_bat_reading = 0;


void system_routine(){

    //Configure Deep sleep switch
    int switch_level = 0;
    int prev_level = switch_level;
    bool pressed = false;
    gpio_set_direction(DEEP_SLEEP_WAKE_UP_PIN,GPIO_MODE_INPUT);
    gpio_intr_enable(DEEP_SLEEP_WAKE_UP_PIN);
    gpio_set_intr_type(DEEP_SLEEP_WAKE_UP_PIN,GPIO_INTR_NEGEDGE);


    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel, atten);
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
    while (1) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            adc_reading += adc1_get_raw((adc1_channel_t)channel);
        }
        adc_reading /= NO_OF_SAMPLES;
        //Convert adc_reading to voltage in mV
        uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
        raw_bat_reading = voltage;
        vTaskDelay(pdMS_TO_TICKS(1000));

        //Setup Deep sleep thingie:

        switch_level = gpio_get_level(DEEP_SLEEP_WAKE_UP_PIN);
        if(prev_level != switch_level){
            if(!switch_level){
                pressed = true;
            }
            prev_level = switch_level;
        }
        if(pressed && BLE_DISCONNECTED){
            rtc_gpio_pulldown_en(DEEP_SLEEP_WAKE_UP_PIN);
            ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(DEEP_SLEEP_WAKE_UP_PIN,DEFAULT_WAKEUP_LEVEL));
            ESP_LOGI("SYSTEM ROUTINE","Going to sleep");
            esp_deep_sleep_start();
        }
        pressed = false;


    }
}


void start_system_routine()
{
    xTaskCreate(system_routine, "measure_battery", 4096, NULL, 5, NULL);
}


