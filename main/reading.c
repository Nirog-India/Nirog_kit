#include "temperature.h"
#include "heartrate.h"
#include "reading.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

reading take_reading(){

    init_heartrate();   //initialize max30120


        xTaskCreate(take_oxy_reading, "oxygen_reading", 4096, NULL, 10, NULL);
        while (finger_not_placed)
        {
            vTaskDelay(10/portTICK_PERIOD_MS);
        }
        xTaskCreate(take_temperature, "temperature_reading", 4096, NULL, 9, NULL);
        while(!isTaskCompleted || !temp_reading_complete){
        vTaskDelay(10/portTICK_PERIOD_MS);             //Wait for readings
        }
        oxy_reading final_oxyreading = get_oxy_result();
        deinit_heartrate();
        reading reading_buffer;
        reading_buffer.heartrate = final_oxyreading.finalheartRate;
        reading_buffer.oxygenLevel = final_oxyreading.oxygenLevel;
        reading_buffer.temperature = temp_reading;
        finger_not_placed = true;

        return reading_buffer;
}