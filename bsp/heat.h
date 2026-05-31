// heat.h (头文件也需要同步更新)
#ifndef __HEAT_H
#define __HEAT_H

#include <stdbool.h>
#include <stdint.h>

#define HEAT_MAX_POWER_PERCENT 100U

/* 注意：heat_init() 仅初始化硬件，不启动PWM；调用 heat_start() 才开始加热 */
bool heat_init(void);

/**
 * @brief 加热模块反初始化
 */
void heat_deinit(void);
bool heat_start(void);
bool heat_stop(void);
bool heat_set_power(uint8_t power_percent);
uint8_t heat_get_current_power(void);

#endif
