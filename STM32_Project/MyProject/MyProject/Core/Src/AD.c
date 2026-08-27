#include "AD.h"
#include "adc.h"
#include "main.h"   // 仅用于 HAL_ADCEx_Calibration_Start，不需要 HAL_Delay

extern ADC_HandleTypeDef hadc1;

// 自定义微秒延时（约 72MHz 主频，粗略延时）
static void delay_us(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 72; i++);
}

void AD_Init(void)
{
    HAL_ADCEx_Calibration_Start(&hadc1);
}

uint16_t AD_GetValue(void)
{
    uint16_t val = 0;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
    {
        val = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return val;
}

// 读取烟雾传感器（PA4），多次平均
uint16_t AD_GetSmoke(void)
{
    uint32_t sum = 0;
    const uint8_t SAMPLES = 10;
    
    // 停止当前转换
    HAL_ADC_Stop(&hadc1);
    
    // 配置通道4（PA4），采样时间加长
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;  // 更稳定
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    HAL_ADC_Start(&hadc1);
    for (int i = 0; i < SAMPLES; i++) {
        if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK) {
            sum += HAL_ADC_GetValue(&hadc1);
        }
        delay_us(10);  // 替换 HAL_Delay(1)，10us 足够
    }
    HAL_ADC_Stop(&hadc1);
    
    uint16_t avg = sum / SAMPLES;
    
    // 恢复通道1（光敏），保持同样采样时间
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    return avg;
}