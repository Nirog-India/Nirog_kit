#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#include "sdkconfig.h"

#include "system_routine.h"
#include "booth_gatt_server.h"


static const char *TAG = "BOOTH Main";



void app_main(void) {

    ESP_LOGI(TAG,"starting Mem %d",xPortGetFreeHeapSize());  
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG,"Nirog Scan");    
    
    rtc_gpio_deinit(DEEP_SLEEP_WAKE_UP_PIN);
    switch (esp_sleep_get_wakeup_cause()){
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(TAG,"Woke up due to trigger");
            break;
        default:
            ESP_LOGI(TAG,"WOKE UP by %d",esp_sleep_get_wakeup_cause());

    }

    start_system_routine();
    start_gatt_server();

}
