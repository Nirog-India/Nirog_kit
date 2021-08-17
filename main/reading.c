#include "temperature.h"
#include "heartrate.h"
#include "reading.h"

#include "freertos/FreeRTOS.h"
#include "MLX90614_SMBus_Driver.h"
#include "freertos/task.h"

reading take_reading(){

    init_heartrate();   //initialize max30120
        xTaskHandle temperature_task = NULL;
        bool prev_finger_state = finger_not_placed;
        xTaskCreate(take_oxy_reading, "oxygen_reading", 4096, NULL, 10, NULL);
        while(!isTaskCompleted || !temp_reading_complete){
            if(prev_finger_state != finger_not_placed){
                if(!finger_not_placed){
                    if(temperature_task != NULL && !temp_reading_complete){
                        printf("deleting\n");
                        close_connection();          // Close all temp sensor i2c connection
                        vTaskDelete(temperature_task);
                        temperature_task = NULL;
                        
                    }
                    xTaskCreate(take_temperature, "temperature_reading", 4096, NULL, 9, &temperature_task);
                }
                prev_finger_state = finger_not_placed;
            }
            vTaskDelay(10/portTICK_PERIOD_MS);             //Wait for readings
        }
        oxy_reading final_oxyreading = get_oxy_result();
        deinit_heartrate();
        reading reading_buffer;
        reading_buffer.heartrate = final_oxyreading.finalheartRate;
        reading_buffer.heart_precision = 100 - final_oxyreading.heart_error;
        reading_buffer.oxygenLevel = final_oxyreading.oxygenLevel;
        reading_buffer.oxy_precision = 100 - final_oxyreading.oxy_error;
        reading_buffer.temperature = temp_reading;
        finger_not_placed = true;
        return reading_buffer;
}