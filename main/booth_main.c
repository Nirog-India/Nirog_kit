#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <sys/types.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifimanager.h"
#include "booth_server.h"
#include "battery_monitor.h"
#include "led_notification.h"



static const char *TAG = "Main";



void app_main(void) {

  for(int i =0;i<6;i++){
  blink_led_start(LED_GRP1,true,true,true);
  vTaskDelay(200/portTICK_PERIOD_MS);
  blink_led_stop();
}
    ESP_LOGI(TAG,"starting Mem %d",xPortGetFreeHeapSize());  
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG,"Booth Demo");    
    wifimanager_start();
    start_battery_check();
    start_webserver();
}
