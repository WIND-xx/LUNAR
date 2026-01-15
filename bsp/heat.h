// heat.h (头文件也需要同步更新)
#ifndef __HEAT_H__
#define __HEAT_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 加热模块最大功率百分比
 */
#define HEAT_MAX_POWER_PERCENT    (100U)

/**
 * @brief 加热模块初始化
 * @retval true - 初始化成功, false - 初始化失败
 */
bool heat_init(void);

/**
 * @brief 加热模块反初始化
 */
void heat_deinit(void);
/**
 * @brief 加热模块启动
 * @retval true - 启动成功, false - 启动失败
 */
bool heat_start(void);
/**
 * @brief 加热模块停止
 * @retval true - 停止成功, false - 停止失败
 */
bool heat_stop(void);

/**
 * @brief 设置加热功率
 * @param power_percent 功率百分比 (0 ~ HEAT_MAX_POWER_PERCENT)
 * @retval true - 设置成功, false - 参数无效
 */
bool heat_set_power(uint8_t power_percent);

/**
 * @brief 获取当前加热功率
 * @retval 当前功率百分比 (0 ~ HEAT_MAX_POWER_PERCENT)
 */
uint8_t heat_get_current_power(void);

#endif /* __HEAT_H__ */
