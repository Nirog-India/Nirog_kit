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
#include "esp_timer.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "MAXheartRate.h"
#include "heartrate.h"
#include "driver/i2c.h"
#include "sdkconfig.h"


static const char *TAG = "Server";

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
float heartsum = 0,oxysum = 0,error = 0,hearterror = 0;
int64_t time_since_start;
bool finger_not_placed = true;
const int tolerance = 7;

// Merges two subarrays of arr[].
// First subarray is arr[l..m]
// Second subarray is arr[m+1..r]
void merge(int arr[], int l, int m, int r)
{
    int i, j, k;
    int n1 = m - l + 1;
    int n2 = r - m;
 
    /* create temp arrays */
    int L[n1], R[n2];
 
    /* Copy data to temp arrays L[] and R[] */
    for (i = 0; i < n1; i++)
        L[i] = arr[l + i];
    for (j = 0; j < n2; j++)
        R[j] = arr[m + 1 + j];
 
    /* Merge the temp arrays back into arr[l..r]*/
    i = 0; // Initial index of first subarray
    j = 0; // Initial index of second subarray
    k = l; // Initial index of merged subarray
    while (i < n1 && j < n2) {
        if (L[i] <= R[j]) {
            arr[k] = L[i];
            i++;
        }
        else {
            arr[k] = R[j];
            j++;
        }
        k++;
    }
 
    /* Copy the remaining elements of L[], if there
    are any */
    while (i < n1) {
        arr[k] = L[i];
        i++;
        k++;
    }
 
    /* Copy the remaining elements of R[], if there
    are any */
    while (j < n2) {
        arr[k] = R[j];
        j++;
        k++;
    }
}
 
/* l is for left index and r is right index of the
sub-array of arr to be sorted */
void mergeSort(int arr[], int l, int r)
{
    if (l < r) {
        // Same as (l+r)/2, but avoids overflow for
        // large l and h
        int m = l + (r - l) / 2;
 
        // Sort first and second halves
        mergeSort(arr, l, m);
        mergeSort(arr, m + 1, r);
 
        merge(arr, l, m, r);
    }
}

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
    isTaskCompleted = false; 
}

void take_oxy_reading() {
    
    
    int valid_count = 0;
    int cnt, samp;
    uint8_t rptr, wptr;
    uint8_t data;
    float spo = 0;
    uint8_t regdata[256];
    float proximity_thresh = 60000;
    
    float red[10],ir[10],red_avg[10],del[5];
    float r_avg=0;
    float spo_avg=0;
    int avg_cnt = 0;
    int count = 0;
    bool valid_read = false;
    float val_error = 0;
    float prev_val = 0;
    float error_arr[5];
    int curr_index = 0;
    float spo_arr[5];
    int peaks_detected = 0;
    int64_t curr_time,prev_time=0;
    bool valid_heart_read = false;
    float hrarray[5];
    int hrarraycnt = 0;
    float hr_avg_arr[5];
    int hr_avg_arr_cnt = 0;
    bool peak_detected = false;
    float hr_avg = 0;
    int threshold = 0;

    bool reading_timeout = false;
    bool heart_read_complete = false;
    bool oxy_read_complete = false;
    float heart_read_err[5];
    int heart_err_index = 0;

    for(int i =0; i<5;i++){ 
        heart_read_err[i] = 20.0;       
        error_arr[i] = 10.0;
        hrarray[i] = 40.0;
        hr_avg_arr[i] = 40 - (i*20); 
    }
    heartsum = 0;
    oxysum = 0;    

    int64_t prev_reading_time = esp_timer_get_time();
    int64_t curr_reading_time = 0;

    int delta = esp_timer_get_time();
    int lastread = 0;
    float bpm =0;
    int RATE_SIZE = 5;
    float rates[RATE_SIZE]; //Array of heart rates
    int rateSpot = 0;
    int beatAvg =0;
    int beatscnt = 0;
    int beats_coll[60];

    while(1){
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
        
        valid_read = true;
        for(cnt = 0; cnt < samp; cnt++){
            red[0] = red[1]; red[1] = red[2];red[2] = red[3]; red[3] = red[4];
            red[4] = red[5]; red[5] = red[6];red[6] = red[7]; red[7] = red[8];
            red[8] = red[9]; red[9] = 256*256*(regdata[6*cnt+0]%4)+ 256*regdata[6*cnt+1]+regdata[6*cnt+2];

            ir[0] = ir[1]; ir[1] = ir[2];ir[2] = ir[3]; ir[3] = ir[4];
            ir[4] = ir[5]; ir[5] = ir[6];ir[6] = ir[7]; ir[7] = ir[8];
            ir[8] = ir[9]; ir[9] = 256*256*(regdata[6*cnt+3]%4)+ 256*regdata[6*cnt+4]+regdata[6*cnt+5];
            // printf("red val : %f\n",red[9]);
            if(red[9] > proximity_thresh){
                float red_DC = (red[0] + red[1] + red[2] + red[3] + red[4] + red[5] + red[6] + red[7] + red[8] + red[9])/10;
                float ir_DC = (ir[0] + ir[1] + ir[2] + ir[3] + ir[4] + ir[5] + ir[6] + ir[7] + ir[8] + ir[9])/10;

                float red_max = 0;
                float red_min = 1000000;
                float ir_max = 0;
                float ir_min = 1000000;

                for (int i=0;i<10;i++){
                    if(red[i]>red_max) red_max = red[i];
                    if(red[i]<red_min) red_min = red[i];
                    if(ir[i]>ir_max) ir_max = ir[i];
                    if(ir[i]<ir_min) ir_min = ir[i];
                }

                float red_pp = red_max - red_min;
                float ir_pp = ir_max - ir_min;
                float r = (ir_pp/ir_DC)/(red_pp/red_DC);
                spo = (-45.060)*r*r+(30.354)*r+94.845; 
                if(spo>100)spo=100;
                if(spo<60)spo=60; 
                r_avg += r;
                spo_avg += spo;
                avg_cnt++; 

                // Heart rate calculations
            for(int i=0;i<9;i++){
    
                red_avg[i] = red_avg[i+1];
            }
            red_avg[9] = (ir[0] + ir[1] + ir[2] + ir[3] + ir[4])/5.0f;      

            //Experiment 
            // printf("IR : %f\t",ir[9]);
            if (checkForBeat(ir[9]) == true){
                delta = esp_timer_get_time() - lastread;
                lastread = esp_timer_get_time();
                beatscnt++;
                bpm = 60000000/(delta);
                printf("beat %f delta %d",bpm,delta);
                 if (bpm < 255 && bpm > 20)
                    {
                    rates[rateSpot++] = bpm; //Store this reading in the array
                    rateSpot %= RATE_SIZE; //Wrap variable

                    //Take average of readings
                    beatAvg = 0;
                    for (int x = 0 ; x < RATE_SIZE ; x++)
                        beatAvg += rates[x];
                    beatAvg /= RATE_SIZE;
                    }
                    printf("max bpm : %f and avg : %d\n",bpm,beatAvg);
                    printf("beats : %d\n",beatscnt);    
                    if(beatscnt>5)beats_coll[beatscnt-6] = beatAvg;
            }
            
      
            for(int i=0;i<4;i++) del[i] = del[i+1];
            del[4] = red_avg[9]-red_avg[8];
            // printf("DEL : %f \tred : %f \n",del[4],red_avg[9]);
            if(!peak_detected){
                peak_detected = true;
            for(int i=0;i<5;i++){
                if(del[i]>threshold)peak_detected = false;
            }
            if(peak_detected){
                peaks_detected++;
            if(peaks_detected >=1){
                curr_time = esp_timer_get_time();
                float hr = (60000000.0f * peaks_detected)/(curr_time - prev_time);
                peaks_detected = 0;
                prev_time = esp_timer_get_time();
                if(hr>1.0){
                    if(hr>200) hr = 200.0;
                    for(int i = 0;i<4;i++){
                            hrarray[i] = hrarray[i+1];	
                            hr_avg_arr[i] = hr_avg_arr[i+1];					
                        }
                
                hrarray[4] = hr;
                hr_avg = (hrarray[0] + hrarray[1] + hrarray[2] + hrarray[3] + hrarray[4])/5.0f;					
                hr_avg_arr[4] = hr_avg;
                    }
                }
            }
        }
            else if(del[0]>0)peak_detected = false;
                  
            }
            else{
                valid_read = false;
            }                
        }
        if(valid_read){  
            finger_not_placed = false;        
            count ++;  
            if(!valid_heart_read){
                valid_heart_read = true;
                prev_time = esp_timer_get_time();
            }          
        }        
        else{            
            finger_not_placed = true;            
            prev_reading_time = esp_timer_get_time();
            }
        if(count>=10){
            valid_count++;                  
            // spo calculations
            float curr_spo = spo_avg/avg_cnt;
            val_error = curr_spo - prev_val;
            prev_val = curr_spo;
            error_arr[curr_index] = (val_error < 0) ? (-1)*val_error : val_error;
            spo_arr[curr_index++] = curr_spo;            
            if(curr_index >= 5){
                curr_index = 0;
            } 
            val_error = hr_avg_arr[4] - hr_avg_arr[3];           
            heart_read_err[heart_err_index++] = (val_error < 0) ? (-1)*val_error : val_error;
            if(heart_err_index>=5) heart_err_index = 0;
            float err_sum = 0;
            float heart_err_sum = 0;
            for(int i=0;i<5;i++){
                heart_err_sum += heart_read_err[i];
                err_sum += error_arr[i];
            }
            float err_avg = err_sum/5;
            float heart_err_avg = heart_err_sum/5;
            // ESP_LOGI(TAG,"\n Ratio :  %f ; spo2 : %f; error : %f ; hr : %f; error : %f",r_avg/avg_cnt,curr_spo,err_avg,hr_avg,heart_err_avg);
            if(err_avg <= 0.5 && !oxy_read_complete){
                for(int i=0;i<5;i++){                    
                    oxysum += spo_arr[i];                    
                }                
                error = err_avg;
                oxy_read_complete = true;

            }
        //     if(heart_err_avg <= 1 && !heart_read_complete){
        //         // heartsum = (hr_avg_arr[0] + hr_avg_arr[1] + hr_avg_arr[2] + hr_avg_arr[3] + hr_avg_arr[4])/5.0f;
        //         mergeSort(beats_coll,0,beatscnt-5);
        //         for(int label=0;label<=beatscnt-5;label++)printf("%d : %d\n",label,beats_coll[label]);
        //                 heartsum = beats_coll[(beatscnt-5)/2];
        //         hearterror = heart_err_avg;					
        //         heart_read_complete = true;
        // }
            if((oxy_read_complete) || reading_timeout){
                if(reading_timeout){
                    if(!oxy_read_complete){
                        for(int i=0;i<5;i++){                    
                            oxysum += spo_arr[i];                    
                        }                
                        error = err_avg;
                    }
                    if(!heart_read_complete){
                        // heartsum = (hr_avg_arr[0] + hr_avg_arr[1] + hr_avg_arr[2] + hr_avg_arr[3] + hr_avg_arr[4])/5.0f;
                        hearterror = heart_err_avg;
                    }
                }
                    mergeSort(beats_coll,0,beatscnt-5);
                    // for(int label=0;label<=beatscnt-5;label++)printf("%d : %d\n",label,beats_coll[label]);
                    heartsum = beats_coll[(beatscnt-5)/2];
                break;                
            }
            curr_reading_time = esp_timer_get_time();
            if((curr_reading_time - prev_reading_time)/1000000ULL > 30 ) reading_timeout = true;

        count = avg_cnt = 0;
        r_avg = spo_avg = 0;
        }
    vTaskDelay(10/portTICK_PERIOD_MS);
    }    

    isTaskCompleted  = true;
    vTaskDelete(NULL);
     
}


esp_timer_handle_t periodic_timer;

void init_heartrate()
{

    i2c_init();
    i2cdetect();
    isTaskCompleted = false;
    max30102_init();    

}

void deinit_heartrate(){
    i2c_driver_delete(i2c_port);
}


static void periodic_timer_callback(void* arg)
{
    time_since_start = esp_timer_get_time();
}



void hr_timer_init(){
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &periodic_timer_callback,
            .name = "periodic"
    };
    
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
}



oxy_reading get_oxy_result(){


    oxy_reading final_readings;
    final_readings.finalheartRate = heartsum;
    final_readings.heart_error = hearterror;
    final_readings.oxygenLevel = oxysum/5;
    final_readings.oxy_error = error;

    return final_readings;
}
