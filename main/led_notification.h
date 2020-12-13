#ifndef _LED_NOTIFICATION_H
#define _LED_NOTIFICATION_H

#define LED_GRP1 1
#define LED_GRP2 2

enum LED{
    RED1_LED = 16,
    BLUE1_LED = 17,
    GREEN1_LED = 18,
    RED2_LED = 12,
    BLUE2_LED = 13,
    GREEN2_LED = 14,

};




#ifdef __cplusplus
extern "C" {
#endif


void led_clear_all();
void led2_clear_all();
void light_led(enum LED led);
void led_error_notification();
void led_success_notification();
void led_pair_notification();
void led_multi_notification(enum LED notify_led1,enum LED notify_led2);
void led_white();
void blink_led_start(int led_num,bool red,bool green,bool blue);
void blink_led_stop();

#ifdef __cplusplus
}
#endif



#endif
