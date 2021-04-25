#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"

#include "sdkconfig.h"

#include "battery_monitor.h"
#include "booth_gatt_server.h"


static const char *TAG = "BOOTH Main";



void app_main(void) {

    ESP_LOGI(TAG,"starting Mem %d",xPortGetFreeHeapSize());  
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG,"Booth Demo");    
    start_battery_check();
    start_gatt_server();

}
