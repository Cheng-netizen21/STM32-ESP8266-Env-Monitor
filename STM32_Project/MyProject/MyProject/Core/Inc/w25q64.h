#ifndef __W25Q64_H
#define __W25Q64_H

#include "main.h"
#include "spi.h"

// ====== 片选引脚定义 ======
#define W25Q64_CS_PIN    GPIO_PIN_12
#define W25Q64_CS_PORT   GPIOB

// ====== 片选控制宏 ======
#define W25Q64_CS_LOW()  HAL_GPIO_WritePin(W25Q64_CS_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET)
#define W25Q64_CS_HIGH() HAL_GPIO_WritePin(W25Q64_CS_PORT, W25Q64_CS_PIN, GPIO_PIN_SET)

// ====== W25Q64 指令集 ======
#define W25Q_CMD_WREN      0x06
#define W25Q_CMD_WRDIS     0x04
#define W25Q_CMD_RDSR1     0x05
#define W25Q_CMD_READ      0x03
#define W25Q_CMD_PP        0x02
#define W25Q_CMD_SE        0x20
#define W25Q_CMD_BE        0xD8
#define W25Q_CMD_CE        0xC7
#define W25Q_CMD_JEDECID   0x9F

// ====== 缓存相关定义（环形扇区缓冲区） ======
#define CACHE_SECTOR_SIZE       4096                    // 4KB 扇区
#define CACHE_DATA_SIZE         16                      // CachedData_t 大小
#define CACHE_PER_SECTOR        (CACHE_SECTOR_SIZE / CACHE_DATA_SIZE)  // 每扇区 256 条
#define CACHE_MAX_SECTORS       4                       // 使用 4 个扇区
#define CACHE_START_ADDR        0x010000                // 缓存起始地址
#define CACHE_INDEX_ADDR        0x000FF0                // 保存缓存状态的位置

// ====== 缓存数据结构（强制紧凑对齐） ======
typedef struct __attribute__((packed)) {
    uint32_t timestamp;
    float    temp;
    float    humi;
    uint16_t light;
    uint16_t smoke;
} CachedData_t;

// ====== 缓存状态结构体（保存在 Flash 中） ======
typedef struct __attribute__((packed)) {
    uint32_t write_sector;      // 当前写入扇区索引 (0 ~ CACHE_MAX_SECTORS-1)
    uint32_t write_offset;      // 当前扇区内写入偏移 (字节)
    uint32_t read_sector;       // 当前读取扇区索引 (0 ~ CACHE_MAX_SECTORS-1)
    uint32_t read_offset;       // 当前扇区内读取偏移 (字节)
} CacheState_t;

// ====== 驱动函数声明 ======
void     W25Q64_Init(void);
uint16_t W25Q64_ReadID(void);
void     W25Q64_WaitBusy(void);
void     W25Q64_SectorErase(uint32_t addr);
void     W25Q64_ReadData(uint32_t addr, uint8_t *buf, uint32_t len);
void     W25Q64_PageWrite(uint32_t addr, uint8_t *buf, uint32_t len);

// ====== 断网缓存接口声明 ======
void     Cache_Init(void);
uint8_t  Cache_Write(CachedData_t *data);
uint8_t  Cache_Read(CachedData_t *data);
uint32_t Cache_GetCount(void);
uint32_t Cache_GetMaxCount(void);
void     Cache_ClearAll(void);

#endif /* __W25Q64_H */
