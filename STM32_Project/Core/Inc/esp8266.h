#ifndef __ESP8266_H__
#define __ESP8266_H__

#include "main.h"
#include <string.h>
#include <stdio.h>

#define ESP8266_RX_BUF_SIZE   512
#define ESP8266_RETRY_MAX     3
#define ESP8266_TIMEOUT_LONG  15000
#define ESP8266_TIMEOUT_SHORT 5000

typedef enum {
    ESP8266_OK       = 0,
    ESP8266_ERROR    = 1,
    ESP8266_TIMEOUT  = 2
} ESP8266_Status_t;

void ESP8266_Init(void);
ESP8266_Status_t ESP8266_SendCmd(const char *cmd, const char *expectedResp,
                                  uint32_t timeout_ms, uint8_t retry);
ESP8266_Status_t ESP8266_ConnectWiFi(const char *ssid, const char *pwd);
ESP8266_Status_t ESP8266_ConnectTCP(const char *ip, uint16_t port);
ESP8266_Status_t ESP8266_SendData(const char *data, uint16_t len);
void ESP8266_UART_RxCallback(uint8_t data);

#endif
