#include "stdio.h"
#include "driver/gpio.h"
#include "led_notification.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


 enum LED notification_led1[3] = {RED1_LED,BLUE1_LED,GREEN1_LED};
enum LED notification_led2[3] = {RED2_LED,BLUE2_LED,GREEN2_LED};

bool red1_on = false;
bool green1_on = false;
bool blue1_on = false;
bool red2_on = false;
bool green2_on = false;
bool blue2_on = false;

int led_number;

void led_clear_all(){
    for(int i = 0; i <3; i++){
        gpio_pad_select_gpio(notification_led1[i]);
        gpio_set_direction(notification_led1[i], GPIO_MODE_OUTPUT);
        gpio_set_level(notification_led1[i],0);    
    }
}

void led2_clear_all(){
    for(int i = 0; i <3; i++){
        gpio_pad_select_gpio(notification_led2[i]);
        gpio_set_direction(notification_led2[i], GPIO_MODE_OUTPUT);
        gpio_set_level(notification_led2[i],0);    
    }
}

void light_led(enum LED notify_led){
    gpio_pad_select_gpio(notify_led);
    gpio_set_direction(notify_led, GPIO_MODE_OUTPUT);
    gpio_set_level(notify_led,1);
}


void led_error_notification(){
    led_clear_all();
    light_led(RED1_LED);
}

void led_success_notification(){
    led_clear_all();
    light_led(GREEN1_LED);
}

void led_pair_notification(){
    led_clear_all();
    light_led(BLUE1_LED);
}

void led_multi_notification(enum LED notify_led1,enum LED notify_led2){
    light_led(notify_led1);
    light_led(notify_led2);
}

void led_white(){
    led_clear_all();
    light_led(GREEN1_LED);
    light_led(BLUE1_LED);
    light_led(RED1_LED);
}

void blink_led_task(){
    while(1){
        led_clear_all();
        vTaskDelay(500/portTICK_RATE_MS);              
        if(red1_on) light_led(RED1_LED);
        if(green1_on) light_led(GREEN1_LED);
        if(blue1_on) light_led(BLUE1_LED);
        if(!red1_on && !green1_on && !blue1_on) break;
        vTaskDelay(500/portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

void blink_led_start(int led_num,bool red,bool green,bool blue){
    led_number = led_num;
    if(led_num == LED_GRP1){
        red1_on = red;
        green1_on = green;
        blue1_on = blue;
    }
    else if(led_num == LED_GRP2){
        red2_on = red;
        green2_on = green;
        blue2_on = blue;
    }
    xTaskCreate(blink_led_task, "blink_led_task", 4096, NULL, 5, NULL);
}

void blink_led_stop(){
    red1_on = false;
    green1_on = false;
    blue1_on = false;
}
