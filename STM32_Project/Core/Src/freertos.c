/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "OLED.h"
#include "AD.h"
#include "DHT11.h"
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "w25q64.h"   // ====== W25Q64 缓存驱动 ======

// ====== 传感器数据包结构体 ======
typedef struct {
    uint32_t timestamp;
    uint8_t temp_int;
    uint8_t temp_dec;
    uint8_t humi_int;
    uint8_t humi_dec;
    uint16_t light;
    int smoke_level;
} SensorData_t;

// 网络状态枚举
typedef enum {
    NET_STATE_UNKNOWN,
    NET_STATE_WIFI_ERR,
    NET_STATE_WIFI_OK,
    NET_STATE_MQTT_OK
} NetState_t;

// 全局网络状态变量
NetState_t g_netState = NET_STATE_UNKNOWN;

extern void IWDG_Feed(void);

// ====== LED和蜂鸣器引脚定义 ======
#define LED_Pin     GPIO_PIN_8
#define LED_GPIO_Port   GPIOA
#define BUZZER_Pin  GPIO_PIN_0
#define BUZZER_GPIO_Port GPIOB

// ====== USART2接收队列 ======
osMessageQueueId_t uart2RxQueueHandle;
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

/* Definitions for myTask01 */
osThreadId_t myTask01Handle;
const osThreadAttr_t myTask01_attributes = {
  .name = "myTask01",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for DisplayTask */
osThreadId_t DisplayTaskHandle;
const osThreadAttr_t DisplayTask_attributes = {
  .name = "DisplayTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for SensorTask */
osThreadId_t SensorTaskHandle;
const osThreadAttr_t SensorTask_attributes = {
  .name = "SensorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for ReportTask */
osThreadId_t ReportTaskHandle;
const osThreadAttr_t ReportTask_attributes = {
  .name = "ReportTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* 两个独立队列 */
osMessageQueueId_t displayQueueHandle;
osMessageQueueId_t reportQueueHandle;

/* Definitions for keySemaphore */
osSemaphoreId_t keySemaphoreHandle;
const osSemaphoreAttr_t keySemaphore_attributes = {
  .name = "keySemaphore"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void StartSensorTask(void *argument);
void StartDisplayTask(void *argument);
void StartReportTask(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void MX_FREERTOS_Init(void);

/**
  * @brief  FreeRTOS initialization
  */
void MX_FREERTOS_Init(void) {
  keySemaphoreHandle = osSemaphoreNew(1, 0, &keySemaphore_attributes);

  displayQueueHandle = osMessageQueueNew(5, sizeof(SensorData_t), NULL);
  reportQueueHandle = osMessageQueueNew(5, sizeof(SensorData_t), NULL);
  uart2RxQueueHandle = osMessageQueueNew(64, sizeof(uint8_t), NULL);

  if (displayQueueHandle == NULL || reportQueueHandle == NULL || uart2RxQueueHandle == NULL) {
      printf("ERROR: Queue creation failed!\r\n");
  } else {
      printf("Queues created successfully.\r\n");
  }

  myTask01Handle = osThreadNew(StartDefaultTask, NULL, &myTask01_attributes);
  DisplayTaskHandle = osThreadNew(StartDisplayTask, NULL, &DisplayTask_attributes);
  SensorTaskHandle = osThreadNew(StartSensorTask, NULL, &SensorTask_attributes);
  ReportTaskHandle = osThreadNew(StartReportTask, NULL, &ReportTask_attributes);
}

void StartDefaultTask(void *argument)
{
  for(;;) osDelay(1);
}

/* USER CODE BEGIN Header_StartSensorTask */
/**
  * @brief  传感器任务：读取 DHT11、光敏和烟雾，发送到两个队列
  */
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void *argument)
{
    SensorData_t sensorData;
    uint32_t tickCount;
    osStatus_t putDisplay, putReport;
    
    uint16_t smoke_raw = 0;
    int smoke_level = 0;

    for(;;)
    {
        tickCount = osKernelGetTickCount();
        sensorData.light = AD_GetValue();
        
        smoke_raw = AD_GetSmoke();
        
        if (smoke_raw <= 500) {
            smoke_level = 0;
        } else if (smoke_raw <= 1500) {
            smoke_level = 1;
        } else {
            smoke_level = 2;
        }

        if (DHT11_Read_Data(&sensorData.temp_int, &sensorData.temp_dec,
                            &sensorData.humi_int, &sensorData.humi_dec) == 0)
        {
            sensorData.timestamp = tickCount;
            sensorData.smoke_level = smoke_level;

            putDisplay = osMessageQueuePut(displayQueueHandle, &sensorData, 0, 100);
            putReport  = osMessageQueuePut(reportQueueHandle, &sensorData, 0, 100);

            if (putDisplay == osOK && putReport == osOK) {
                printf("Sensor: put OK, temp=%d.%d, humi=%d.%d, light=%d, smoke=%d(%d)\r\n",
                       sensorData.temp_int, sensorData.temp_dec,
                       sensorData.humi_int, sensorData.humi_dec,
                       sensorData.light, smoke_level, smoke_raw);
            } else {
                printf("Sensor: queue put error (display=%d, report=%d)\r\n", putDisplay, putReport);
            }
            
            // ====== 边缘联动报警 ======
            if (sensorData.temp_int >= 30 || smoke_level == 2) {
                HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
                printf("ALERT! temp=%d, smoke_level=%d\r\n", sensorData.temp_int, smoke_level);
                HAL_UART_Transmit(&huart2, (uint8_t*)"ALERT\r\n", 7, 100);
            } 
            else if (sensorData.temp_int <= 28 && smoke_level == 0) {
                HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
            }
        }
        else
        {
            printf("Sensor: DHT11 read failed\r\n");
        }

        IWDG_Feed();
        osDelay(1000);
    }
}

/* USER CODE BEGIN Header_StartDisplayTask */
/**
  * @brief  显示任务：从 displayQueue 获取数据并刷新 OLED
  */
/* USER CODE END Header_StartDisplayTask */
void StartDisplayTask(void *argument)
{
    SensorData_t sensorData;
    char buf[16];

    for(;;)
    {
        if (osMessageQueueGet(displayQueueHandle, &sensorData, NULL, osWaitForever) == osOK)
        {
            OLED_ShowString(1, 1, "Temp:");
            OLED_ShowNum(1, 6, sensorData.temp_int, 2);
            OLED_ShowChar(1, 8, '.');
            OLED_ShowNum(1, 9, sensorData.temp_dec, 1);
            OLED_ShowChar(1, 10, 'C');

            OLED_ShowString(2, 1, "Humi:");
            OLED_ShowNum(2, 6, sensorData.humi_int, 2);
            OLED_ShowChar(2, 8, '.');
            OLED_ShowNum(2, 9, sensorData.humi_dec, 1);
            OLED_ShowChar(2, 10, '%');

            OLED_ShowString(3, 1, "Light:");
            sprintf(buf, "%4d", sensorData.light);
            OLED_ShowString(3, 7, buf);

            OLED_ShowString(4, 1, "Smoke:");
            OLED_ShowNum(4, 7, sensorData.smoke_level, 1);
            if (sensorData.smoke_level == 0) {
                OLED_ShowString(4, 9, "N");
            } else if (sensorData.smoke_level == 1) {
                OLED_ShowString(4, 9, "W");
            } else {
                OLED_ShowString(4, 9, "A");
            }
        }
        osDelay(50);
    }
}

/* USER CODE BEGIN Header_StartReportTask */
/**
  * @brief  上报任务：发送传感器数据给ESP8266，并从队列读取USART2接收的数据
  * @note   根据网络状态决定发送或缓存，网络恢复后自动补传
  */
/* USER CODE END Header_StartReportTask */
void StartReportTask(void *argument)
{
    SensorData_t data;
    char tx_buf[64];
    char rx_buf[64];
    uint8_t rx_idx = 0;
    NetState_t newState = NET_STATE_UNKNOWN;
    uint8_t rx_byte;
    
    // ====== 记录上一次网络状态，用于检测状态变化 ======
    static NetState_t lastState = NET_STATE_UNKNOWN;

    printf("=== Report Task Start (send via USART2) ===\r\n");

    for(;;)
    {
        // ====== 从中断接收队列读取数据 ======
        while (osMessageQueueGet(uart2RxQueueHandle, &rx_byte, NULL, 0) == osOK) {            
            if (rx_byte == '\n' || rx_byte == '\r') {
                if (rx_idx > 0) {
                    rx_buf[rx_idx] = '\0';
                    
                    // ====== LED控制 ======
                    if (strstr(rx_buf, "CMD:LED_ON")) {
                        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
                        printf("LED ON\r\n");
                        HAL_UART_Transmit(&huart2, (uint8_t*)"STA:LED_OK\r\n", 12, 100);
                    } else if (strstr(rx_buf, "CMD:LED_OFF")) {
                        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
                        printf("LED OFF\r\n");
                        HAL_UART_Transmit(&huart2, (uint8_t*)"STA:LED_OK\r\n", 12, 100);
                    } 
                    // ====== 蜂鸣器控制 ======
                    else if (strstr(rx_buf, "CMD:BUZZER_ON")) {
                        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
                        printf("BUZZER ON\r\n");
                        HAL_UART_Transmit(&huart2, (uint8_t*)"STA:BUZZER_OK\r\n", 15, 100);
                    } else if (strstr(rx_buf, "CMD:BUZZER_OFF")) {
                        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
                        printf("BUZZER OFF\r\n");
                        HAL_UART_Transmit(&huart2, (uint8_t*)"STA:BUZZER_OK\r\n", 15, 100);
                    } 
                    // ====== 网络状态指令 ======
                    else if (strstr(rx_buf, "STA:WIFI_OK")) {
                        newState = NET_STATE_WIFI_OK;
                        printf("Net state: WIFI_OK\r\n");
                    } else if (strstr(rx_buf, "STA:MQTT_OK")) {
                        newState = NET_STATE_MQTT_OK;
                        printf("Net state: MQTT_OK\r\n");
                    } else if (strstr(rx_buf, "STA:WIFI_ERR")) {
                        newState = NET_STATE_WIFI_ERR;
                        printf("Net state: WIFI_ERR\r\n");
                    } 
                    // ====== 网络恢复指令（备用触发方式） ======
                    else if (strstr(rx_buf, "NET_RECOVER")) {
                        printf("NET_RECOVER received, checking cache...\r\n");
                        uint32_t count = Cache_GetCount();
                        printf("Cache count: %lu\r\n", count);
                        
                        if (count > 0) {
                            printf("Sending cached data...\r\n");
                            CachedData_t cached;
                            char buf[64];
                            uint32_t sent_count = 0;
                            
                            while (Cache_Read(&cached)) {
                                snprintf(buf, sizeof(buf), "T:%.1f,H:%.1f,L:%d\r\n", 
                                         cached.temp, cached.humi, cached.light);
                                
                                if (HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 100) == HAL_OK) {
                                    printf("Cache sent: %s", buf);
                                    sent_count++;
                                } else {
                                    printf("Cache send failed, retry later\r\n");
                                    break;
                                }
                                osDelay(50);
                            }
                            printf("Cache sent done, total: %lu\r\n", sent_count);
                        }
                    } else if (strlen(rx_buf) > 0) {
                        printf("Unknown: %s\r\n", rx_buf);
                    }
                }
                rx_idx = 0;
            } else {
                if (rx_idx < sizeof(rx_buf) - 1) {
                    rx_buf[rx_idx++] = rx_byte;
                }
            }
        }

        // ====== 发送传感器数据 ======
        if (osMessageQueueGet(reportQueueHandle, &data, NULL, 500) == osOK) {
            float temp = data.temp_int + data.temp_dec / 10.0f;
            float humi = data.humi_int + data.humi_dec / 10.0f;
            snprintf(tx_buf, sizeof(tx_buf), "T:%.1f,H:%.1f,L:%d\r\n", temp, humi, data.light);

            // ====== 根据网络状态决定发送或缓存 ======
            if (g_netState == NET_STATE_MQTT_OK) {
                // 网络在线，尝试发送
                if (HAL_UART_Transmit(&huart2, (uint8_t*)tx_buf, strlen(tx_buf), 100) == HAL_OK) {
                    printf("Sent to ESP8266: %s", tx_buf);
                } else {
                    // UART发送失败（硬件错误），缓存
                    CachedData_t cached;
                    cached.timestamp = data.timestamp;
                    cached.temp = temp;
                    cached.humi = humi;
                    cached.light = data.light;
                    cached.smoke = data.smoke_level;
                    Cache_Write(&cached);
                    printf("UART error, cached: %lu\r\n", Cache_GetCount());
                }
            } else {
                // ====== 网络离线，直接缓存 ======
                printf("Offline, caching data...\r\n");
                CachedData_t cached;
                cached.timestamp = data.timestamp;
                cached.temp = temp;
                cached.humi = humi;
                cached.light = data.light;
                cached.smoke = data.smoke_level;
                Cache_Write(&cached);
                printf("Cached, total: %lu\r\n", Cache_GetCount());
            }
        }

        // ====== 检测网络状态变化：从离线变为在线时自动补传 ======
        if (newState == NET_STATE_MQTT_OK && lastState != NET_STATE_MQTT_OK) {
            uint32_t count = Cache_GetCount();
            if (count > 0) {
                printf("MQTT reconnected, sending %lu cached data...\r\n", count);
                CachedData_t cached;
                char buf[64];
                uint32_t sent_count = 0;
                
                while (Cache_Read(&cached)) {
                    snprintf(buf, sizeof(buf), "T:%.1f,H:%.1f,L:%d\r\n", 
                             cached.temp, cached.humi, cached.light);
                    
                    if (HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 100) == HAL_OK) {
                        printf("Cache sent: %s", buf);
                        sent_count++;
                    } else {
                        printf("Cache send failed, retry later\r\n");
                        break;
                    }
                    osDelay(50);
                }
                printf("Cache sent done: %lu\r\n", sent_count);
            }
        }
        lastState = newState;

        // ====== 更新全局网络状态 ======
        g_netState = newState;

        osDelay(100);
    }
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
