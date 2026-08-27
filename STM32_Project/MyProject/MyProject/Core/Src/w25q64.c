#include "w25q64.h"
#include <stdio.h>
#include <string.h>

// ============================================================
// ====== SPI 底层操作 ======
// ============================================================

static uint8_t W25Q64_SendByte(uint8_t byte)
{
    uint8_t rx_byte = 0;
    HAL_SPI_TransmitReceive(&hspi1, &byte, &rx_byte, 1, HAL_MAX_DELAY);
    return rx_byte;
}

static void W25Q64_WriteEnable(void)
{
    W25Q64_CS_LOW();
    W25Q64_SendByte(W25Q_CMD_WREN);
    W25Q64_CS_HIGH();
}

void W25Q64_WaitBusy(void)
{
    uint8_t status = 0xFF;
    do {
        W25Q64_CS_LOW();
        W25Q64_SendByte(W25Q_CMD_RDSR1);
        status = W25Q64_SendByte(0xFF);
        W25Q64_CS_HIGH();
    } while (status & 0x01);
}

void W25Q64_Init(void)
{
    // ====== 替换 HAL_Delay(10) 为简单循环延时 ======
    for (volatile uint32_t i = 0; i < 10 * 6000; i++);   // 约 10ms (假设 72MHz)

    W25Q64_CS_HIGH();

    uint16_t id = W25Q64_ReadID();
    printf("W25Q64 ID: 0x%04X\r\n", id);

    if (id == 0xEF16 || id == 0xEF40) {  // 兼容不同批次
        printf("W25Q64 detected!\r\n");
    } else {
        printf("W25Q64 not detected! ID=0x%04X\r\n", id);
    }

    Cache_Init();
}

uint16_t W25Q64_ReadID(void)
{
    uint8_t buf[2] = {0};
    
    W25Q64_CS_LOW();
    W25Q64_SendByte(W25Q_CMD_JEDECID);
    buf[0] = W25Q64_SendByte(0xFF);
    buf[1] = W25Q64_SendByte(0xFF);
    W25Q64_CS_HIGH();
    
    return (buf[0] << 8) | buf[1];
}

void W25Q64_SectorErase(uint32_t addr)
{
    W25Q64_WriteEnable();
    W25Q64_WaitBusy();
    
    W25Q64_CS_LOW();
    W25Q64_SendByte(W25Q_CMD_SE);
    W25Q64_SendByte((addr >> 16) & 0xFF);
    W25Q64_SendByte((addr >> 8) & 0xFF);
    W25Q64_SendByte(addr & 0xFF);
    W25Q64_CS_HIGH();
    
    W25Q64_WaitBusy();
}

void W25Q64_ReadData(uint32_t addr, uint8_t *buf, uint32_t len)
{
    W25Q64_CS_LOW();
    W25Q64_SendByte(W25Q_CMD_READ);
    W25Q64_SendByte((addr >> 16) & 0xFF);
    W25Q64_SendByte((addr >> 8) & 0xFF);
    W25Q64_SendByte(addr & 0xFF);
    
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = W25Q64_SendByte(0xFF);
    }
    
    W25Q64_CS_HIGH();
}

void W25Q64_PageWrite(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (len == 0 || len > 256) return;
    
    W25Q64_WriteEnable();
    W25Q64_WaitBusy();
    
    W25Q64_CS_LOW();
    W25Q64_SendByte(W25Q_CMD_PP);
    W25Q64_SendByte((addr >> 16) & 0xFF);
    W25Q64_SendByte((addr >> 8) & 0xFF);
    W25Q64_SendByte(addr & 0xFF);
    
    for (uint32_t i = 0; i < len; i++) {
        W25Q64_SendByte(buf[i]);
    }
    
    W25Q64_CS_HIGH();
    W25Q64_WaitBusy();
}

// ============================================================
// ====== 断网缓存实现（环形扇区缓冲区） ======
// ============================================================

static CacheState_t cache_state;  // 当前缓存状态（内存中的副本）

// ====== 获取扇区起始地址 ======
static uint32_t Cache_GetSectorAddr(uint32_t sector_idx)
{
    return CACHE_START_ADDR + sector_idx * CACHE_SECTOR_SIZE;
}

// ====== 初始化缓存（从 Flash 恢复状态） ======
void Cache_Init(void)
{
    // 从 Flash 读取缓存状态
    W25Q64_ReadData(CACHE_INDEX_ADDR, (uint8_t*)&cache_state, sizeof(CacheState_t));
    
    // 校验状态有效性
    if (cache_state.write_sector >= CACHE_MAX_SECTORS ||
        cache_state.read_sector >= CACHE_MAX_SECTORS ||
        cache_state.write_offset >= CACHE_SECTOR_SIZE ||
        cache_state.read_offset >= CACHE_SECTOR_SIZE) {
        // 状态无效，重置
        cache_state.write_sector = 0;
        cache_state.write_offset = 0;
        cache_state.read_sector = 0;
        cache_state.read_offset = 0;
        W25Q64_PageWrite(CACHE_INDEX_ADDR, (uint8_t*)&cache_state, sizeof(CacheState_t));
    }
    
    uint32_t count = Cache_GetCount();
    printf("Cache: write_sector=%lu, read_sector=%lu, count=%lu\r\n",
           cache_state.write_sector, cache_state.read_sector, count);
}

// ====== 写入一条缓存数据 ======
uint8_t Cache_Write(CachedData_t *data)
{
    uint32_t sector_addr = Cache_GetSectorAddr(cache_state.write_sector);
    uint32_t write_addr = sector_addr + cache_state.write_offset;
    
    // 检查当前扇区是否已满
    if (cache_state.write_offset + CACHE_DATA_SIZE > CACHE_SECTOR_SIZE) {
        // 当前扇区已满，切换到下一个扇区
        cache_state.write_sector++;
        if (cache_state.write_sector >= CACHE_MAX_SECTORS) {
            cache_state.write_sector = 0;
        }
        cache_state.write_offset = 0;
        
        // 擦除新扇区
        uint32_t new_sector_addr = Cache_GetSectorAddr(cache_state.write_sector);
        W25Q64_SectorErase(new_sector_addr);
        
        // 如果新扇区刚好是读扇区，说明读指针已被覆盖，重置读指针
        if (cache_state.write_sector == cache_state.read_sector) {
            cache_state.read_sector = (cache_state.write_sector + 1) % CACHE_MAX_SECTORS;
            cache_state.read_offset = 0;
        }
        
        sector_addr = new_sector_addr;
        write_addr = sector_addr;
    }
    
    // 写入数据
    W25Q64_PageWrite(write_addr, (uint8_t*)data, CACHE_DATA_SIZE);
    cache_state.write_offset += CACHE_DATA_SIZE;
    
    // 保存状态到 Flash
    W25Q64_PageWrite(CACHE_INDEX_ADDR, (uint8_t*)&cache_state, sizeof(CacheState_t));
    
    return 1;
}

// ====== 读取并删除最早的一条缓存数据 ======
uint8_t Cache_Read(CachedData_t *data)
{
    uint32_t count = Cache_GetCount();
    if (count == 0) return 0;
    
    // 读取当前读指针位置的数据
    uint32_t sector_addr = Cache_GetSectorAddr(cache_state.read_sector);
    uint32_t read_addr = sector_addr + cache_state.read_offset;
    W25Q64_ReadData(read_addr, (uint8_t*)data, CACHE_DATA_SIZE);
    
    // 移动读指针
    cache_state.read_offset += CACHE_DATA_SIZE;
    
    // 检查读指针是否超出当前扇区
    if (cache_state.read_offset + CACHE_DATA_SIZE > CACHE_SECTOR_SIZE) {
        // 当前扇区已读完，切换到下一个扇区
        cache_state.read_sector++;
        if (cache_state.read_sector >= CACHE_MAX_SECTORS) {
            cache_state.read_sector = 0;
        }
        cache_state.read_offset = 0;
    }
    
    // 如果读指针追上写指针，重置缓存
    if (cache_state.read_sector == cache_state.write_sector &&
        cache_state.read_offset >= cache_state.write_offset) {
        // 缓存已空，重置状态
        cache_state.read_sector = 0;
        cache_state.read_offset = 0;
        cache_state.write_sector = 0;
        cache_state.write_offset = 0;
        // 擦除第一个扇区，准备重新写入
        W25Q64_SectorErase(Cache_GetSectorAddr(0));
    }
    
    // 保存状态到 Flash
    W25Q64_PageWrite(CACHE_INDEX_ADDR, (uint8_t*)&cache_state, sizeof(CacheState_t));
    
    return 1;
}

// ====== 获取当前缓存条数 ======
uint32_t Cache_GetCount(void)
{
    uint32_t total = 0;
    
    // 计算写扇区和读扇区之间的差值
    int32_t sector_diff = (int32_t)cache_state.write_sector - (int32_t)cache_state.read_sector;
    if (sector_diff < 0) {
        sector_diff += CACHE_MAX_SECTORS;
    }
    
    // 计算总数据条数
    total = sector_diff * CACHE_PER_SECTOR;
    total += (cache_state.write_offset - cache_state.read_offset) / CACHE_DATA_SIZE;
    
    // 如果写和读在同一个扇区，offset可能为0
    if (total > CACHE_MAX_SECTORS * CACHE_PER_SECTOR) {
        total = CACHE_MAX_SECTORS * CACHE_PER_SECTOR;
    }
    
    return total;
}

// ====== 获取最大缓存条数 ======
uint32_t Cache_GetMaxCount(void)
{
    return CACHE_MAX_SECTORS * CACHE_PER_SECTOR;
}

// ====== 清空所有缓存 ======
void Cache_ClearAll(void)
{
    cache_state.write_sector = 0;
    cache_state.write_offset = 0;
    cache_state.read_sector = 0;
    cache_state.read_offset = 0;
    
    // 擦除第一个扇区
    W25Q64_SectorErase(Cache_GetSectorAddr(0));
    
    // 保存状态
    W25Q64_PageWrite(CACHE_INDEX_ADDR, (uint8_t*)&cache_state, sizeof(CacheState_t));
    printf("Cache cleared\r\n");
}
