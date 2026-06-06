/**
 * @file    bsp_key.h
 * @brief   矩阵键盘驱动（纯硬件扫描，无 OS 依赖）
 * @version 2.0
 * @date    2026-06-06
 */

#ifndef BSP_KEY_H
#define BSP_KEY_H

#include "bsp_common.h"
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 类型定义
 *============================================================================*/

/** 不透明键盘句柄 */
BSP_HANDLE_DECLARE(key);

/** 行配置：驱动引脚 + 列引脚列表 + 对应键值 */
typedef struct {
    uint16_t output_pin;    /**< 行驱动输出引脚 */
    uint16_t input_pins[5]; /**< 列输入引脚列表（0=无效） */
    uint8_t key_values[5];  /**< 对应按键返回值（0=无效） */
} bsp_key_row_t;

/** 外部电源检测引脚配置（可选） */
typedef struct {
    GPIO_TypeDef* port; /**< 电源检测端口 */
    uint16_t pin;       /**< 电源检测引脚 */
    uint8_t key_value;  /**< 电源键返回值 */
} bsp_key_power_pin_t;

/** 键盘配置 */
typedef struct {
    const bsp_key_row_t* rows;            /**< 行配置表 */
    uint8_t row_count;                    /**< 行数 */
    const bsp_key_power_pin_t* power_pin; /**< 电源检测引脚（NULL=无） */
    GPIO_TypeDef* row_port;               /**< 行引脚所在 GPIO 端口（通常 GPIOB） */
} bsp_key_config_t;

/*==============================================================================
 * API 函数
 *============================================================================*/

/**
 * @brief 初始化键盘
 * @param[out] handle  返回的键盘句柄
 * @param[in]  config  硬件配置
 * @return BSP_OK 成功
 */
bsp_status_t bsp_key_init(bsp_key_t** handle, const bsp_key_config_t* config);

/**
 * @brief 反初始化键盘（释放资源）
 * @param handle 句柄指针的指针
 */
void bsp_key_deinit(bsp_key_t** handle);

/**
 * @brief 扫描键盘，获取当前按下的键值
 * @param handle  键盘句柄
 * @param[out] key_value  返回的键值（0 = 无按键）
 * @return BSP_OK 成功
 *
 * @note  每次调用进行一次完整矩阵扫描（逐行驱动、逐列读取）。
 *         典型调用周期 20ms。
 */
bsp_status_t bsp_key_scan(bsp_key_t* handle, uint8_t* key_value);

/**
 * @brief 检查电源键是否按下（独立于矩阵扫描）
 * @param handle 键盘句柄
 * @return true=按下, false=未按下或无电源键配置
 */
bool bsp_key_is_power_pressed(bsp_key_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_KEY_H */
