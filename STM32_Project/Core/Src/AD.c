#include "AD.h"
#include "adc.h"      // CubeMX 生成的 adc.h，包含 hadc1 声明

extern ADC_HandleTypeDef hadc1;

void AD_Init(void)
{
    // STM32F1xx HAL 库校准函数只需要一个参数
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

// ====== 新增：读取烟雾传感器（PA4，ADC通道4） ======
uint16_t AD_GetSmoke(void)
{
    uint16_t val = 0;
    
    // 临时切换到通道4（PA4）
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
    
    // 先停止ADC，配置通道，再启动
    HAL_ADC_Stop(&hadc1);
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
    {
        val = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    
    // 切换回原来的通道1（PA1），避免影响光敏读取
    sConfig.Channel = ADC_CHANNEL_1;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    return val;
}
