/**
 * @file    bsp_ntc.h
 * @brief   NTC 温度传感器驱动（查表法 + 插值 + DMA，无 OS 依赖）
 * @version 2.0
 * @date    2026-06-06
 *
 * @note    支持 DMA 双缓冲模式和非 DMA 轮询模式。
 *          滤波链：中值滤波 → 滑动平均 → 低通滤波。
 *          通过编译宏 NTC_USE_DMA / USE_DOUBLE_BUFFER 控制。
 */

#ifndef BSP_NTC_H
#define BSP_NTC_H

#include "bsp_common.h"
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 编译期配置
 *============================================================================*/

#define NTC_USE_DMA 1           /**< 启用 DMA 采样 */
#define USE_DOUBLE_BUFFER 1     /**< 启用 DMA 双缓冲 */
#define NTC_FILTER_SAMPLES 5    /**< 中值滤波样本数（奇数） */
#define NTC_MOVING_AVG_LEN 8    /**< 滑动平均长度（2 的幂） */
#define NTC_LOW_PASS_ALPHA 0.3f /**< 低通滤波系数 */

/*==============================================================================
 * 类型定义
 *============================================================================*/

/** 不透明 NTC 句柄 */
BSP_HANDLE_DECLARE(ntc);

/** NTC 配置 */
typedef struct {
    ADC_HandleTypeDef* hadc; /**< HAL ADC 句柄 */
    float temp_offset;       /**< 温度补偿偏移量（℃） */
} bsp_ntc_config_t;

/*==============================================================================
 * API 函数
 *============================================================================*/

/**
 * @brief 初始化 NTC 传感器
 * @param[out] handle  返回的 NTC 句柄
 * @param[in]  config  ADC 句柄及配置
 * @return BSP_OK 成功
 */
bsp_status_t bsp_ntc_init(bsp_ntc_t** handle, const bsp_ntc_config_t* config);

/**
 * @brief 反初始化 NTC（释放 DMA 和内存）
 * @param handle 句柄指针的指针
 */
void bsp_ntc_deinit(bsp_ntc_t** handle);

/**
 * @brief 读取当前温度
 * @param handle       NTC 句柄
 * @param[out] temp_c  返回温度值（℃）
 * @return BSP_OK 成功，BSP_ERR_HW 传感器故障
 */
bsp_status_t bsp_ntc_read(bsp_ntc_t* handle, float* temp_c);

/**
 * @brief 获取原始 ADC 值（经过滤波）
 * @param handle        NTC 句柄
 * @param[out] adc_val  返回 ADC 值 [0, 4095]
 * @return BSP_OK 成功
 */
bsp_status_t bsp_ntc_get_adc(bsp_ntc_t* handle, uint32_t* adc_val);

/**
 * @brief 查询传感器是否故障（开路/短路/卡死）
 * @return true=故障
 */
bool bsp_ntc_is_fault(bsp_ntc_t* handle);

/**
 * @brief 清除故障状态
 */
void bsp_ntc_clear_fault(bsp_ntc_t* handle);

/**
 * @brief 设置温度补偿偏移
 */
void bsp_ntc_set_offset(bsp_ntc_t* handle, float offset);

/**
 * @brief 重置滤波器（丢弃历史数据）
 */
void bsp_ntc_reset_filter(bsp_ntc_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_NTC_H */
