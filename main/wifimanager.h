#ifndef WIFIMANAGER_H_
#define WIFIMANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

void wifi_onConnection();
void erase_gpio_init();
uint8_t erase_gpio_state();

extern int wifiHaveConfig;
int wifimanager_start();
int wifimanager_connected();
bool websocket_connect_wifi_handler(char* wifi_cred);

#ifdef __cplusplus
}
#endif

#endif