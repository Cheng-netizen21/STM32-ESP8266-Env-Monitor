#include "esp8266.h"
#include "usart.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

static uint8_t rx_buf[ESP8266_RX_BUF_SIZE];
static uint16_t rx_head = 0;
static uint16_t rx_tail = 0;

void ESP8266_UART_RxCallback(uint8_t data)
{
    uint16_t next = (rx_head + 1) % ESP8266_RX_BUF_SIZE;
    if (next != rx_tail) {
        rx_buf[rx_head] = data;
        rx_head = next;
    }
}

static int ESP8266_GetChar(void)
{
    if (rx_tail == rx_head) return -1;
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % ESP8266_RX_BUF_SIZE;
    return c;
}

static void ESP8266_FlushBuffer(void)
{
    while (ESP8266_GetChar() != -1);
    rx_head = 0;
    rx_tail = 0;
}

static void ESP8266_SendString(const char *str)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)str, strlen(str), 1000);
}

static uint8_t ESP8266_WaitResponse(const char *expected, uint32_t timeout_ms)
{
    char tmp[ESP8266_RX_BUF_SIZE];
    uint16_t cnt = 0;
    uint32_t start = osKernelGetTickCount();
    
    while ((osKernelGetTickCount() - start) < timeout_ms) {
        int c = ESP8266_GetChar();
        if (c != -1 && cnt < sizeof(tmp) - 1) {
            tmp[cnt++] = (char)c;
            tmp[cnt] = '\0';
            if (strstr(tmp, expected) != NULL) return 1;
        }
        osDelay(10);
    }
    return 0;
}

void ESP8266_Init(void)
{
    ESP8266_FlushBuffer();
    ESP8266_SendString("AT\r\n");
    osDelay(500);
    ESP8266_FlushBuffer();
    printf("[ESP8266] Init Done\r\n");
}

ESP8266_Status_t ESP8266_SendCmd(const char *cmd, const char *expectedResp,
                                  uint32_t timeout_ms, uint8_t retry)
{
    for (uint8_t i = 0; i <= retry; i++) {
        ESP8266_FlushBuffer();
        ESP8266_SendString(cmd);
        printf("Sent: %s\r\n", cmd);
        ESP8266_SendString("\r\n");
        
        if (ESP8266_WaitResponse(expectedResp, timeout_ms)) {
            return ESP8266_OK;
        }
        printf("[ESP8266] Retry %d: %s\r\n", i+1, cmd);
        osDelay(500);
    }
    return ESP8266_TIMEOUT;
}

ESP8266_Status_t ESP8266_ConnectWiFi(const char *ssid, const char *pwd)
{
    char cmd[128];
    
    printf("[ESP8266] Connecting to WiFi: %s\r\n", ssid);
    
    if (ESP8266_SendCmd("AT+CWMODE=1", "OK", ESP8266_TIMEOUT_SHORT, ESP8266_RETRY_MAX) != ESP8266_OK)
        return ESP8266_ERROR;
    osDelay(200);
    
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    if (ESP8266_SendCmd(cmd, "OK", ESP8266_TIMEOUT_LONG, ESP8266_RETRY_MAX) != ESP8266_OK)
        return ESP8266_ERROR;
    
    printf("[ESP8266] WiFi Connected!\r\n");
    return ESP8266_OK;
}

ESP8266_Status_t ESP8266_ConnectTCP(const char *ip, uint16_t port)
{
    char cmd[128];
    
    printf("[ESP8266] Connecting to TCP Server: %s:%d\r\n", ip, port);
    
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%d", ip, port);
    if (ESP8266_SendCmd(cmd, "CONNECT", ESP8266_TIMEOUT_LONG, ESP8266_RETRY_MAX) != ESP8266_OK)
        return ESP8266_ERROR;
    printf("[ESP8266] TCP Connected!\r\n");
    return ESP8266_OK;
}

ESP8266_Status_t ESP8266_SendData(const char *data, uint16_t len)
{
    char cmd[32];
    
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", len);
    if (ESP8266_SendCmd(cmd, ">", ESP8266_TIMEOUT_SHORT, ESP8266_RETRY_MAX) != ESP8266_OK)
        return ESP8266_ERROR;
    
    ESP8266_FlushBuffer();
    ESP8266_SendString(data);
    
    if (ESP8266_WaitResponse("SEND OK", ESP8266_TIMEOUT_SHORT))
        return ESP8266_OK;
    
    return ESP8266_ERROR;
}
