#ifndef _BATTERY_MONITOR_
#define _BATTERY_MONITOR_

#define DEEP_SLEEP_WAKE_UP_PIN 32
#define DEFAULT_WAKEUP_LEVEL    1
bool BLE_DISCONNECTED;

void start_system_routine();
extern uint32_t raw_bat_reading;

#endif 