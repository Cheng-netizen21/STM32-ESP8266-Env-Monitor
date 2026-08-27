#ifndef __AD_H
#define __AD_H

#include <stdint.h>
#include "main.h"

void AD_Init(void);
uint16_t AD_GetValue(void);

// ====== 新增：烟雾传感器读取函数（PA4） ======
uint16_t AD_GetSmoke(void);

#endif
