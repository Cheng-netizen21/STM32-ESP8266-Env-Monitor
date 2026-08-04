#include "DHT11.h"

// 微秒延时函数（粗略延时，适用于 72MHz 系统时钟）
// 如果时序要求严格，建议改用定时器实现，此处提供循环延时版本
static void delay_us(uint32_t us)
{
    uint32_t count = us * 8;   // 72MHz 下，循环约 8 个周期/微秒（需根据实际调整）
    while (count--);
}

// 设置 GPIO 为输出模式（用于主机发送起始信号）
static void DHT11_SetOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;   // 推挽输出
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

// 设置 GPIO 为输入模式（用于读取 DHT11 响应和数据）
static void DHT11_SetInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;       // 输入模式
    GPIO_InitStruct.Pull = GPIO_PULLUP;           // 内部上拉（可选）
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

// 主机发送起始信号，并等待 DHT11 响应
// 返回值：0 表示成功，1 表示无响应或超时
static uint8_t DHT11_Start(void)
{
    // 1. 设置为输出，拉低总线至少 18ms
    DHT11_SetOutput();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);                      // 延时 20ms（大于 18ms）

    // 2. 拉高总线 20-40us，然后释放总线（转为输入）
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    delay_us(30);                       // 延时 30us
    DHT11_SetInput();                   // 转为输入，总线被上拉电阻拉高

    // 3. 等待 DHT11 响应（低电平 80us）
    uint32_t timeout = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
    {
        if (++timeout > 5000) return 1; // 超时
        delay_us(1);
    }
    // 等待低电平结束（约 80us）
    timeout = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET)
    {
        if (++timeout > 5000) return 1;
        delay_us(1);
    }
    // 等待高电平（约 80us）
    timeout = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
    {
        if (++timeout > 5000) return 1;
        delay_us(1);
    }
    return 0;   // 起始成功
}

// 读取一个位（0 或 1）
static uint8_t DHT11_ReadBit(void)
{
    // 等待低电平（约 50us）
    uint32_t timeout = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET)
    {
        if (++timeout > 5000) return 2; // 超时错误
        delay_us(1);
    }
    // 测量高电平持续时间
    uint32_t high_time = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
    {
        if (++high_time > 5000) return 2;
        delay_us(1);
    }
    // 高电平时间 > 30us 视为 1，否则为 0（典型：26-28us 为 0，70us 为 1）
    if (high_time > 30) return 1;
    else return 0;
}

// 读取一个字节（8 位）
static uint8_t DHT11_ReadByte(void)
{
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t bit = DHT11_ReadBit();
        if (bit == 2) return 0xFF;     // 读取错误
        data = (data << 1) | bit;
    }
    return data;
}

// 初始化 DHT11 引脚（使总线处于空闲高电平）
void DHT11_Init(void)
{
    // 使能 GPIO 时钟（CubeMX 已经使能，但为了安全再次确保）
    __HAL_RCC_GPIOA_CLK_ENABLE();
    DHT11_SetOutput();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);  // 总线空闲高电平
}

// 读取一次温湿度数据（整数部分和小数部分）
// 返回值：0 成功，1 失败
uint8_t DHT11_Read_Data(uint8_t *temp_int, uint8_t *temp_dec,
                        uint8_t *humi_int, uint8_t *humi_dec)
{
    uint8_t buf[5] = {0};

    // 1. 发送起始信号并等待响应
    if (DHT11_Start() != 0) return 1;

    // 2. 连续读取 40 位数据（5 个字节）
    for (uint8_t i = 0; i < 5; i++)
    {
        buf[i] = DHT11_ReadByte();
        if (buf[i] == 0xFF) return 1;   // 读取错误
    }

    // 3. 校验和验证
    if ((buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) return 1;

    // 4. 分离数据（整数和小数部分，DHT11 小数部分通常为 0）
    *humi_int = buf[0];
    *humi_dec = buf[1];
    *temp_int = buf[2];
    *temp_dec = buf[3];

    return 0;
}