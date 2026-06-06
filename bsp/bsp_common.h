/**
 * @file    bsp_common.h
 * @brief   BSP 层统一基础类型定义
 * @note    所有 BSP 模块应包含此头文件，遵循统一接口规范：
 *          - 返回值统一使用 bsp_status_t
 *          - 命名约定：bsp_{module}_{verb}_{noun}()
 *          - 实例化句柄：不透明指针 bsp_xxx_t*，由 bsp_xxx_init() 创建
 *          - 必须实现：init / deinit 成对出现
 *          - 禁止直接依赖 FreeRTOS 或任何 OS
 * @version 1.0
 * @date    2026-06-06
 */

#ifndef BSP_COMMON_H
#define BSP_COMMON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 统一状态码
 *============================================================================*/

typedef enum {
    BSP_OK          =  0,  /**< 操作成功 */
    BSP_ERROR       = -1,  /**< 通用错误 */
    BSP_ERR_PARAM   = -2,  /**< 参数无效（NULL、越界等） */
    BSP_ERR_BUSY    = -3,  /**< 资源忙（重复初始化、DMA占用等） */
    BSP_ERR_TIMEOUT = -4,  /**< 操作超时 */
    BSP_ERR_HW      = -5,  /**< 硬件错误（外设故障、传感器异常等） */
    BSP_ERR_NOMEM   = -6,  /**< 内存不足 */
    BSP_ERR_NOTINIT = -7,  /**< 模块未初始化 */
} bsp_status_t;

/*==============================================================================
 * Handle 模式基础宏
 *============================================================================*/

/**
 * @brief 声明一个不透明的 BSP 句柄类型
 * @param name 模块名（小写），如 ntc / led / buzzer
 *
 * 用法：
 *   BSP_HANDLE_DECLARE(ntc);   // → typedef struct bsp_ntc_s bsp_ntc_t;
 *   在 .c 中定义 struct bsp_ntc_s { ... };
 *   外部仅通过 bsp_ntc_t* 指针访问
 */
#define BSP_HANDLE_DECLARE(name) \
    typedef struct bsp_##name##_s bsp_##name##_t

/*==============================================================================
 * 通用位操作（BSP 层专用）
 *============================================================================*/

#define BSP_BIT_SET(reg, bit)    ((reg) |= (bit))
#define BSP_BIT_CLEAR(reg, bit)  ((reg) &= ~(bit))
#define BSP_BIT_CHECK(reg, bit)  (((reg) & (bit)) != 0)

/*==============================================================================
 * 通用工具宏
 *============================================================================*/

/** 数组元素个数 */
#define BSP_ARRAY_SIZE(arr)  (sizeof(arr) / sizeof((arr)[0]))

/** 安全释放指针（置 NULL 防悬空） */
#define BSP_SAFE_FREE(ptr)   do { if (ptr) { free(ptr); (ptr) = NULL; } } while (0)

/** 限制值在 [min, max] 范围内 */
#define BSP_CLAMP(val, min, max)  \
    (((val) < (min)) ? (min) : (((val) > (max)) ? (max) : (val)))

#ifdef __cplusplus
}
#endif

#endif /* BSP_COMMON_H */
