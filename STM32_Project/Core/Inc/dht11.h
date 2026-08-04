#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"   // HAL 库主头文件，包含所有类型定义

// ----- 用户可修改的引脚定义 -----
#define DHT11_PORT      GPIOA           // 选择 GPIOA 组
#define DHT11_PIN       GPIO_PIN_0      // 选择 PA4 引脚

// ----- 函数声明 -----
void DHT11_Init(void);                  // 初始化 DHT11 引脚（设置为输出高电平）
uint8_t DHT11_Read_Data(uint8_t *temp_int, uint8_t *temp_dec,
                        uint8_t *humi_int, uint8_t *humi_dec);  // 读取温湿度

#endif