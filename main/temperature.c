
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "MLX90614_API.h"
#include "MLX90614_SMBus_Driver.h"

#include "temperature.h"

static const char *TAG = "Server";

#define MLX90614_DEFAULT_ADDRESS 0x5a // default chip address(slave address) of MLX90614

#define MLX90614_SDA_GPIO 4 // sda for MLX90614
#define MLX90614_SCL_GPIO 5 // scl for MLX90614
#define TIME_S 35
const float temp_offset = 4;


void take_temperature()
{
    
    temp_reading_complete = false;
   MLX90614_SMBusInit(MLX90614_SDA_GPIO, MLX90614_SCL_GPIO, 50000); // sda scl and 50kHz
    printf("temp reading started\n");
    //Average out the readings
    float to = 0; // temperature of object
    float ta = 0; // temperature of ambient
    float obj_temp_sum = 0;
    uint16_t dumpInfo = 0;
    // loop
    int count = 0;
    while (count<TIME_S)
    {
        // printf("test-data-log:%lf \r\n", temp);
        MLX90614_GetTo(MLX90614_DEFAULT_ADDRESS, &to);
        MLX90614_GetTa(MLX90614_DEFAULT_ADDRESS, &ta);
        MLX90614_GetTa(MLX90614_DEFAULT_ADDRESS, &dumpInfo);
        // ESP_LOGI(TAG,"log:%lf %d\r\n",to,dumpInfo);
        vTaskDelay(100/portTICK_RATE_MS);
        if(count > 10)
            {obj_temp_sum += to;}
        count++;
    }
    close_connection();
    temp_reading = (obj_temp_sum/(count-11))+temp_offset;
    temp_reading_complete = true;
    printf("\n Temp reading finished  : %f \n",temp_reading);
    vTaskDelete(NULL);
}

