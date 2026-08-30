/**
  * @brief  极简 IAP：预擦除 APP 区域 + 接收超时重试（ESP 代码不变）
  * @硬件   STM32F103C8, USART1 调试, USART2 与 ESP 通信
  */
#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>

#define APP_START_ADDRESS   0x08003000
#define BAUD_RATE           57600
#define WAIT_TIMEOUT_MS     2000

#define OTA_FLAG_ADDR       0x0800FC00
#define OTA_FLAG_VALUE      0x5A5A5A5A
#define FLASH_PAGE_SIZE     1024
#define RX_BLOCK_SIZE       1024
#define DATA_ACK_BYTE       0x06

static void USART1_Init(uint32_t baudrate);
static void USART2_Init(uint32_t baudrate);
static void SerialPutChar(uint8_t c);
static void SerialPutString(const uint8_t *str);
static uint8_t Serial_GetChar(uint8_t *ch);
static void DelayMs(uint32_t ms);
static void JumpToApp(void);
static void OTA_ReceiveAndWrite(void);
static void USART2_SendByte(uint8_t byte);
static int  USART2_GetChar(void);
static void write_buffer_to_flash(uint8_t *buf, uint16_t len,
                                  uint32_t *flash_addr_ptr,
                                  uint32_t *total_received_ptr);
static void USART2_Flush(void);

static uint32_t JumpAddress;
typedef void (*pFunction)(void);
static pFunction Jump_To_Application;

int main(void)
{
    uint8_t ch = 0;
    uint32_t timeout = 0;
    uint8_t enterIAP = 0;
    uint8_t enterOTA = 0;
    int i;

    USART1_Init(BAUD_RATE);
    USART2_Init(BAUD_RATE);

    SerialPutString((uint8_t*)"\r\n=== IAP Bootloader ===\r\n");
    SerialPutString((uint8_t*)"Press 'U' to jump APP, 'A' for OTA upgrade\r\n");
    SerialPutString((uint8_t*)"Waiting 2 seconds...\r\n");

    timeout = 0;
    while (timeout < WAIT_TIMEOUT_MS) {
        if (Serial_GetChar(&ch)) {
            if (ch == 'U' || ch == 'u') {
                enterIAP = 1;
                break;
            } else if (ch == 'A' || ch == 'a') {
                enterOTA = 1;
                break;
            }
        }
        DelayMs(10);
        timeout += 10;
    }

    if (*(__IO uint32_t*)OTA_FLAG_ADDR == OTA_FLAG_VALUE) {
        FLASH_Unlock();
        FLASH_ErasePage(OTA_FLAG_ADDR);
        FLASH_ProgramWord(OTA_FLAG_ADDR, 0x00000000);
        FLASH_Lock();
        SerialPutString((uint8_t*)"OTA flag found, jumping to APP...\r\n");
        JumpToApp();
        return 0;
    }

    if (enterIAP) {
        SerialPutString((uint8_t*)"\r\nJump to APP now...\r\n");
        for (i = 0; i < 100000; i++);
        JumpToApp();
    } else if (enterOTA) {
        SerialPutString((uint8_t*)"\r\nEntering OTA receive mode...\r\n");
        SerialPutString((uint8_t*)"Waiting for ESP data on USART2...\r\n");
        OTA_ReceiveAndWrite();
        SerialPutString((uint8_t*)"OTA complete. Jump to APP now...\r\n");
        DelayMs(200);
        JumpToApp();
    } else {
        SerialPutString((uint8_t*)"\r\nTimeout, jump to APP...\r\n");
        for (i = 0; i < 100000; i++);
        JumpToApp();
    }

    while (1) {}
}

static void JumpToApp(void)
{
    uint32_t app_sp, app_pc;
    int i;
    char dbg[64];

    app_sp = *(__IO uint32_t*)APP_START_ADDRESS;
    app_pc = *(__IO uint32_t*)(APP_START_ADDRESS + 4);

    sprintf(dbg, "Vector: SP=0x%08X, PC=0x%08X\r\n", app_sp, app_pc);
    SerialPutString((uint8_t*)dbg);

    if (app_sp < 0x20000000 || app_sp > 0x20005000 || app_pc < 0x08003000 || app_pc > 0x08010000) {
        SerialPutString((uint8_t*)"ERROR: Invalid APP vector table!\r\n");
        while (1) {}
    }

    __disable_irq();
    for (i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    RCC->APB1RSTR = 0xFFFFFFFF;
    RCC->APB2RSTR = 0xFFFFFFFF;
    RCC->APB1RSTR = 0;
    RCC->APB2RSTR = 0;
    RCC->AHBENR  = 0;
    RCC->APB1ENR = 0;
    RCC->APB2ENR = 0;

    GPIOA->CRL = 0x44444444; GPIOA->CRH = 0x44444444;
    GPIOB->CRL = 0x44444444; GPIOB->CRH = 0x44444444;
    GPIOC->CRL = 0x44444444; GPIOC->CRH = 0x44444444;

    SCB->VTOR = APP_START_ADDRESS;
    __DSB();
    __ISB();

    __set_MSP(app_sp);
    JumpAddress = app_pc;
    Jump_To_Application = (pFunction)JumpAddress;
    Jump_To_Application();

    while (1) {}
}

// ========== OTA 接收函数（预擦除 + 超时重试） ==========
static void OTA_ReceiveAndWrite(void)
{
    uint32_t flash_addr = APP_START_ADDRESS;
    uint32_t total_received = 0;
    uint32_t expected_len = 0;
    uint8_t rx_buf[RX_BLOCK_SIZE];
    uint16_t rx_idx = 0;
    int c_int;
    uint8_t i;
    uint32_t retry_count, timeout;
    char msg[32];
    uint32_t no_data_timeout;

    FLASH_Unlock();

    // ====== 1. 预擦除整个 APP 区域（0x08003000 ~ 0x0800FFFF） ======
    SerialPutString((uint8_t*)"Pre-erasing APP area...\r\n");
    uint32_t erase_addr = APP_START_ADDRESS;
    while (erase_addr < 0x08010000) {   // 擦到芯片末尾（64KB），安全
        FLASH_ErasePage(erase_addr);
        erase_addr += FLASH_PAGE_SIZE;
    }
    SerialPutString((uint8_t*)"Pre-erase done.\r\n");

    // ====== 2. 复位 USART2 清除错误标志 ======
    USART2_Flush();

    while (1) {
        flash_addr = APP_START_ADDRESS;
        total_received = 0;
        expected_len = 0;
        rx_idx = 0;

        SerialPutString((uint8_t*)"Waiting for handshake on USART2...\r\n");

        retry_count = 0;
        while (retry_count < 10) {
            USART2_Flush();
            USART2_SendByte(0x55);
            timeout = 0;
            while (timeout < 2000000) {
                c_int = USART2_GetChar();
                if (c_int == 0xAA) break;
                timeout++;
            }
            if (c_int == 0xAA) {
                SerialPutString((uint8_t*)"Handshake OK\r\n");
                break;
            }
            retry_count++;
            SerialPutString((uint8_t*)"Retry handshake...\r\n");
        }
        if (retry_count >= 10) {
            SerialPutString((uint8_t*)"Handshake timeout\r\n");
            FLASH_Lock();
            return;
        }

        expected_len = 0;
        for (i = 0; i < 4; i++) {
            while (1) {
                c_int = USART2_GetChar();
                if (c_int >= 0) break;
            }
            expected_len = (expected_len << 8) | (uint32_t)(uint8_t)c_int;
        }

        if (expected_len == 0 || expected_len > (0x08010000 - APP_START_ADDRESS)) {
            SerialPutString((uint8_t*)"ERROR: Invalid length, restarting...\r\n");
            continue;
        }

        sprintf(msg, "Firmware length: %lu bytes\r\n", expected_len);
        SerialPutString((uint8_t*)msg);
        SerialPutString((uint8_t*)"Receiving firmware...\r\n");

        no_data_timeout = 0;
        while (total_received < expected_len) {
            uint32_t remaining = expected_len - total_received;
            uint16_t need = (remaining < RX_BLOCK_SIZE) ? (uint16_t)remaining : RX_BLOCK_SIZE;
            rx_idx = 0;
            while (rx_idx < need) {
                c_int = USART2_GetChar();
                if (c_int >= 0) {
                    rx_buf[rx_idx++] = (uint8_t)c_int;
                    no_data_timeout = 0;
                } else {
                    no_data_timeout++;
                    if (no_data_timeout > 50000) { // 约 5 秒无数据（每循环约 100us）
                        SerialPutString((uint8_t*)"OTA: Receive timeout, restarting handshake...\r\n");
                        goto restart_handshake;
                    }
                }
            }
            // 写入 Flash（此时无需擦除，因为已预擦除）
            write_buffer_to_flash(rx_buf, need, &flash_addr, &total_received);
            USART2_SendByte(DATA_ACK_BYTE);
            sprintf(msg, "OTA: %lu / %lu bytes\r\n", total_received, expected_len);
            SerialPutString((uint8_t*)msg);
        }

        sprintf(msg, "OTA: Total %lu bytes written\r\n", total_received);
        SerialPutString((uint8_t*)msg);
        SerialPutString((uint8_t*)"OTA: Firmware received OK!\r\n");
        break;

restart_handshake:
        // 重新进入握手
        continue;
    }

    FLASH_Lock();
}

static void write_buffer_to_flash(uint8_t *buf, uint16_t len,
                                  uint32_t *flash_addr_ptr,
                                  uint32_t *total_received_ptr)
{
    uint32_t flash_addr = *flash_addr_ptr;
    uint32_t word = 0;
    uint8_t byte_idx = 0;
    uint16_t i;

    for (i = 0; i < len; i++) {
        word |= (buf[i] << (byte_idx * 8));
        byte_idx++;
        if (byte_idx == 4) {
            // 已预擦除，直接编程
            FLASH_ProgramWord(flash_addr, word);
            flash_addr += 4;
            *total_received_ptr += 4;
            byte_idx = 0;
            word = 0;
        }
    }

    if (byte_idx > 0) {
        while (byte_idx < 4) {
            word |= (0xFF << (byte_idx * 8));
            byte_idx++;
        }
        FLASH_ProgramWord(flash_addr, word);
        *total_received_ptr += (len % 4);
    }
    *flash_addr_ptr = flash_addr;
}

// ========== 串口函数（与之前一致） ==========
static void USART1_Init(uint32_t baudrate)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    USART_InitTypeDef USART_InitStruct;
    USART_InitStruct.USART_BaudRate = baudrate;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStruct);
    USART_Cmd(USART1, ENABLE);
}

static void USART2_Init(uint32_t baudrate)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    USART_InitTypeDef USART_InitStruct;
    USART_InitStruct.USART_BaudRate = baudrate;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStruct);
    USART_Cmd(USART2, ENABLE);
}

static void SerialPutChar(uint8_t c)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, c);
}

static void SerialPutString(const uint8_t *str)
{
    while (*str) SerialPutChar(*str++);
}

static uint8_t Serial_GetChar(uint8_t *ch)
{
    if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET) {
        *ch = USART_ReceiveData(USART1);
        return 1;
    }
    return 0;
}

static void USART2_SendByte(uint8_t byte)
{
    USART_SendData(USART2, byte);
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

static int USART2_GetChar(void)
{
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) == SET) {
        USART_ReceiveData(USART2);
        return -1;
    }
    if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET) {
        return (int)(uint8_t)USART_ReceiveData(USART2);
    }
    return -1;
}

static void USART2_Flush(void)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET) {
        (void)USART_ReceiveData(USART2);
    }
}

static void DelayMs(uint32_t ms)
{
    for (; ms > 0; ms--) {
        for (volatile uint32_t i = 0; i < 6000; i++);
    }
}

void SystemInit(void) { /* 空 */ }
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line) { while (1); }
#endif
