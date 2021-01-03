#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "driver/i2c.h"
#include "sdkconfig.h"
#include "heartrate.h"

#define I2C_ADDR_MAX30102      0x57 //max30102 i2c address

#include "i2c.h"

#define TOTAL_READ  30
const int spo2_offset = 45;

//globals
char outStr[1500];
float meastime;
int countedsamples = 0;
int irpower = 0, rpower = 0, lirpower = 0, lrpower = 0;
int startstop = 0, raworbp = 0;
float heartrate=99.2, pctspo2=99.2;  
bool isTaskCompleted = false; 
float heartsum = 0,oxysum = 0;



void max30102_init() {
    uint8_t data;
    data = ( 0x2 << 5);  //sample averaging 0=1,1=2,2=4,3=8,4=16,5+=32
    i2c_write(I2C_ADDR_MAX30102, 0x08, data);
    data = 0x03;                //mode = red and ir samples
    i2c_write(I2C_ADDR_MAX30102, 0x09, data);
    data = ( 0x3 << 5) + ( 0x3 << 2 ) + 0x3; //first and last 0x3, middle smap rate 0=50,1=100,etc 
    i2c_write(I2C_ADDR_MAX30102, 0x0a, data);
    data = 0xd0;                //ir pulse power
    i2c_write(I2C_ADDR_MAX30102, 0x0c, data);
    data = 0xa0;                //red pulse power
    i2c_write(I2C_ADDR_MAX30102, 0x0d, data);
}

void max30102_task () {
    
    
    int valid_count = 0;
    int cnt, samp, tcnt = 0, cntr = 0;
    uint8_t rptr, wptr;
    uint8_t data;
    uint8_t regdata[256];
    //int irmeas, redmeas;
    float firxv[5], firyv[5], fredxv[5], fredyv[5];
    float lastmeastime = 0;
    float hrarray[10],spo2array[10];
    int hrarraycnt = 0;
    heartsum = 0;
    oxysum = 0;
    while(valid_count < TOTAL_READ){
        if(lirpower!=irpower){
            data = (uint8_t) irpower;
            i2c_write(I2C_ADDR_MAX30102, 0x0d,  data); 
            lirpower=irpower;
        }
        if(lrpower!=rpower){
            data = (uint8_t) rpower;
            i2c_write(I2C_ADDR_MAX30102, 0x0c,  data); 
            lrpower=rpower;
        }
        i2c_read(I2C_ADDR_MAX30102, 0x04, &wptr, 1);
        i2c_read(I2C_ADDR_MAX30102, 0x06, &rptr, 1);
        samp = ((32+wptr)-rptr)%32;
        i2c_read(I2C_ADDR_MAX30102, 0x07, regdata, 6*samp);
        for(cnt = 0; cnt < samp; cnt++){
            meastime =  0.01 * tcnt++;
            firxv[0] = firxv[1]; firxv[1] = firxv[2]; firxv[2] = firxv[3]; firxv[3] = firxv[4];
            firxv[4] = (1/3.48311) * (256*256*(regdata[6*cnt+3]%4)+ 256*regdata[6*cnt+4]+regdata[6*cnt+5]);
            firyv[0] = firyv[1]; firyv[1] = firyv[2]; firyv[2] = firyv[3]; firyv[3] = firyv[4];
            firyv[4] = (firxv[0] + firxv[4]) - 2 * firxv[2]
                    + ( -0.1718123813 * firyv[0]) + (  0.3686645260 * firyv[1])
                    + ( -1.1718123813 * firyv[2]) + (  1.9738037992 * firyv[3]);

            fredxv[0] = fredxv[1]; fredxv[1] = fredxv[2]; fredxv[2] = fredxv[3]; fredxv[3] = fredxv[4];
            fredxv[4] = (1/3.48311) * (256*256*(regdata[6*cnt+0]%4)+ 256*regdata[6*cnt+1]+regdata[6*cnt+2]);
            fredyv[0] = fredyv[1]; fredyv[1] = fredyv[2]; fredyv[2] = fredyv[3]; fredyv[3] = fredyv[4];
            fredyv[4] = (fredxv[0] + fredxv[4]) - 2 * fredxv[2]
                    + ( -0.1718123813 * fredyv[0]) + (  0.3686645260 * fredyv[1])
                    + ( -1.1718123813 * fredyv[2]) + (  1.9738037992 * fredyv[3]);
            if (-1.0 * firyv[4] >= 100 && -1.0 * firyv[2] > -1*firyv[0] && -1.0 * firyv[2] > -1*firyv[4] && meastime-lastmeastime > 0.5){
               hrarray[hrarraycnt%5] = 60 / (meastime - lastmeastime);
               spo2array[hrarraycnt%5] = 110 - 25 * ((fredyv[4]/fredxv[4]) / (firyv[4]/firxv[4]));
               if(spo2array[hrarraycnt%5]>100)spo2array[hrarraycnt%5]=99.9;
               printf ("%6.2f  %4.2f     hr= %5.1f     spo2= %5.1f\n", meastime, meastime - lastmeastime, heartrate, pctspo2);
               lastmeastime = meastime;
               valid_count ++;
               if(valid_count > TOTAL_READ-10){
                   heartsum += heartrate ;
                   oxysum += pctspo2;
               } 
	       hrarraycnt++;
	       float curr_heartrate = (hrarray[0]+hrarray[1]+hrarray[2]+hrarray[3]+hrarray[4]) / 5;
	       if (curr_heartrate < 40 || curr_heartrate > 150) continue;//heartrate = 0;
           else heartrate = curr_heartrate;
	       float curr_pctspo2 = (spo2array[0]+spo2array[1]+spo2array[2]+spo2array[3]+spo2array[4]) / 5;
	       if (curr_pctspo2 < 50 || curr_pctspo2 > 100) continue;//pctspo2 = 0;
           else pctspo2 = curr_pctspo2;
            }
            
        }

    }    

    isTaskCompleted  = true;
     vTaskDelete(NULL);
}

void max30102_test_task () {
    
    
    int valid_count = 0;
    int cnt, samp, tcnt = 0, cntr = 0;
    uint8_t rptr, wptr;
    uint8_t data;
    uint8_t regdata[256];
    //int irmeas, redmeas;
    float firxv[5], firyv[5], fredxv[5], fredyv[5];
    float lastmeastime = 0;
    float hrarray[10],spo2array[10];
    int hrarraycnt = 0;
    heartsum = 0;
    oxysum = 0;
    while(valid_count < TOTAL_READ){
        if(lirpower!=irpower){
            data = (uint8_t) irpower;
            i2c_write(I2C_ADDR_MAX30102, 0x0d,  data); 
            lirpower=irpower;
        }
        if(lrpower!=rpower){
            data = (uint8_t) rpower;
            i2c_write(I2C_ADDR_MAX30102, 0x0c,  data); 
            lrpower=rpower;
        }
        i2c_read(I2C_ADDR_MAX30102, 0x04, &wptr, 1);
        i2c_read(I2C_ADDR_MAX30102, 0x06, &rptr, 1);
        samp = ((32+wptr)-rptr)%32;
        i2c_read(I2C_ADDR_MAX30102, 0x07, regdata, 6*samp);        
        for(cnt = 0; cnt < samp; cnt++){

        
            meastime =  0.01 * tcnt++;
            firxv[0] = firxv[1]; firxv[1] = firxv[2]; firxv[2] = firxv[3]; firxv[3] = firxv[4];
            firxv[4] = (1/3.48311) * (256*256*(regdata[6*cnt+3]%4)+ 256*regdata[6*cnt+4]+regdata[6*cnt+5]);
            firyv[0] = firyv[1]; firyv[1] = firyv[2]; firyv[2] = firyv[3]; firyv[3] = firyv[4];
            firyv[4] = (firxv[0] + firxv[4]) - 2 * firxv[2]
                    + ( -0.1718123813 * firyv[0]) + (  0.3686645260 * firyv[1])
                    + ( -1.1718123813 * firyv[2]) + (  1.9738037992 * firyv[3]);

            fredxv[0] = fredxv[1]; fredxv[1] = fredxv[2]; fredxv[2] = fredxv[3]; fredxv[3] = fredxv[4];
            fredxv[4] = (1/3.48311) * (256*256*(regdata[6*cnt+0]%4)+ 256*regdata[6*cnt+1]+regdata[6*cnt+2]);
            fredyv[0] = fredyv[1]; fredyv[1] = fredyv[2]; fredyv[2] = fredyv[3]; fredyv[3] = fredyv[4];
            fredyv[4] = (fredxv[0] + fredxv[4]) - 2 * fredxv[2]
                    + ( -0.1718123813 * fredyv[0]) + (  0.3686645260 * fredyv[1])
                    + ( -1.1718123813 * fredyv[2]) + (  1.9738037992 * fredyv[3]);

            if (-1.0 * firyv[4] >= 100 && -1.0 * firyv[2] > -1*firyv[0] && -1.0 * firyv[2] > -1*firyv[4] && meastime-lastmeastime > 0.5){
               hrarray[hrarraycnt%5] = 60 / (meastime - lastmeastime);
               spo2array[hrarraycnt%5] = 110 - 25 * ((fredyv[4]/fredxv[4]) / (firyv[4]/firxv[4]));
               if(spo2array[hrarraycnt%5]>100)spo2array[hrarraycnt%5]=99.9;
               lastmeastime = meastime;
               valid_count ++;
               if(valid_count > TOTAL_READ-10){
                   heartsum += heartrate ;
                   oxysum += pctspo2;
               } 
	       hrarraycnt++;
	       heartrate = (hrarray[0]+hrarray[1]+hrarray[2]+hrarray[3]+hrarray[4]) / 5;
	       if (heartrate < 40 || heartrate > 150) heartrate = 0;
	       pctspo2 = (spo2array[0]+spo2array[1]+spo2array[2]+spo2array[3]+spo2array[4]) / 5;
	       if (pctspo2 < 50 || pctspo2 > 101) pctspo2 = 0;
            }
        }

    }    

    isTaskCompleted  = true;
     vTaskDelete(NULL);
}

void get_heartrate()
{

    i2c_init();
    i2cdetect();
    isTaskCompleted = false;
    //configure max30102 with i2c instructions
    max30102_init();    
    xTaskCreate(max30102_task, "max30102_task", 4096, NULL, 10, NULL);
    while(!isTaskCompleted){
        vTaskDelay(10/portTICK_PERIOD_MS);             //Wait for readings
    }

}

oxy_reading get_oxy_result(){

    get_heartrate();

    oxy_reading final_readings;
    final_readings.finalheartRate = heartsum/10;
    final_readings.oxygenLevel = (oxysum/10)+spo2_offset;

    return final_readings;
}
