#ifndef __ESP8266_H__
#define __ESP8266_H__

#include "hal_types.h"

void ESP8266_Init(void);
void ESP8266_Tick(void);
uint8 ESP8266_IsConnected(void);
void ESP8266_SetSensorData(int16 temp_x10, uint16 humi_x10, uint8 light);

// WiFi status for OLED display
extern uint8 g_wifiState;   // 0=init, 1=connecting, 2=ok, 3=fail
extern uint8 g_wifiClient;  // 1=TCP client connected

#endif
