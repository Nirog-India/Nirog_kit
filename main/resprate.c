
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   64          //Multisampling
#define TIME_IN_S   30

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel = ADC_CHANNEL_0;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

static void check_efuse()
{
    //Check TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }

    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
}

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}

void push_number(uint32_t arr[], uint32_t num){
    arr[0] = arr[1];
    arr[1] = arr[2];
    arr[2] = num;    
}
int get_resp_rate(){

    //Check if Two Point or Vref are burned into eFuse
    check_efuse();

    //Configure ADC
    if (unit == ADC_UNIT_1) {
        adc1_config_width(ADC_WIDTH_BIT_10);
        adc1_config_channel_atten(channel, atten);
    } else {
        adc2_config_channel_atten((adc2_channel_t)channel, atten);
    }

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

    //Continuously sample ADC1
    uint16_t count = 0;
    uint32_t prev_reading = 0;
    uint32_t prev_3_diffs[3] = {0,0,0};
    uint8_t breath_count = 0;
    int prev_delta = 0;

    while (count < (TIME_IN_S * 10)) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            if (unit == ADC_UNIT_1) {
                adc_reading += adc1_get_raw((adc1_channel_t)channel);
            } else {
                int raw;
                adc2_get_raw((adc2_channel_t)channel, ADC_WIDTH_BIT_12, &raw);
                adc_reading += raw;
            }
        }
        adc_reading /= NO_OF_SAMPLES;        
        //Convert adc_reading to voltage in mV
        //uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
        uint32_t diff_mag = abs(adc_reading - prev_reading);        
        push_number(prev_3_diffs,diff_mag);
        
        int delta = 0;
        for(int i=0;i<3;i++){
            if(prev_3_diffs[i] > 40 && prev_3_diffs[i] < 400){
                delta = 1;
            }
        }
        //printf("%d,%d,%d\t%d\n",prev_3_diffs[0],prev_3_diffs[1],prev_3_diffs[2],delta);
        if(prev_delta == 0 && delta == 1){
            breath_count++;
            printf("Breathing detected\n");
        }
        prev_delta = delta;
        prev_reading = adc_reading;
        count++;

        //printf("%d\n",diff);
        //printf("Raw: %d\tVoltage: %dmV\n", adc_reading, voltage);        
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("Respiration Rate: %d per minute\n", breath_count * 2);   
    return breath_count*2;
        
}


