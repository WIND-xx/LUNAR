/**
 * @file app_config.h
 * @brief 应用集中配置文件（所有可调参数统一管理）
 * @version 1.0
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 任务配置
 *============================================================================*/
#define TASK_BLE_NAME "ble_task"
#define TASK_BLE_STACK 512
#define TASK_BLE_PRIORITY 5

#define TASK_HEAT_NAME "heat_task"
#define TASK_HEAT_STACK 512
#define TASK_HEAT_PRIORITY 3

#define TASK_KEY_NAME "key_scan"
#define TASK_KEY_STACK 256
#define TASK_KEY_PRIORITY 2

/*============================================================================
 * 加热控制配置
 *============================================================================*/
#define HEAT_CONTROL_PERIOD_MS 500  // 控制周期（ms）
#define HEAT_MAX_TIMER_MIN 720      // 最大定时（分钟）
#define HEAT_NTC_FAIL_THRESHOLD 3   // NTC连续失败阈值
#define HEAT_BUZZER_BEEP_COUNT 5    // 档位边界蜂鸣次数
#define HEAT_MUTEX_TIMEOUT_MS 50    // 互斥锁超时（ms）
#define HEAT_QUEUE_LEN 5            // 控制队列长度

/*============================================================================
 * PID 参数
 *============================================================================*/
#define HEAT_PID_KP 20.0f
#define HEAT_PID_KI 3.0f
#define HEAT_PID_KD 1.0f
#define HEAT_PID_DEADBAND 1.0f
#define HEAT_PID_OUT_MIN 0
#define HEAT_PID_OUT_MAX 100
#define HEAT_PID_INTEGRAL_LIMIT 30.0f
#define HEAT_PID_D_FILTER 0.1f

/*============================================================================
 * 按键配置
 *============================================================================*/
#define KEY_SCAN_INTERVAL_MS 20
#define KEY_LONG_PRESS_MS 2000
#define KEY_DOUBLE_CLICK_MS 500
#define KEY_LONG_PRESS_TICKS (KEY_LONG_PRESS_MS / KEY_SCAN_INTERVAL_MS)

/*============================================================================
 * NTC 温度采样配置
 *============================================================================*/
#define NTC_MEDIAN_SAMPLES 5
#define NTC_MOVING_AVG_LEN 8
#define NTC_LOW_PASS_ALPHA 0.3f
#define NTC_ADC_MAX 4095
#define NTC_TEMP_MIN (-20.0f)
#define NTC_TEMP_MAX 100.0f
#define NTC_STUCK_THRESHOLD 10  // 传感器卡死检测阈值（次）

/*============================================================================
 * BLE / DMA 配置
 *============================================================================*/
#define BLE_DMA_BUF_SIZE 128
#define BLE_DMA_BUF_COUNT 4
#define BLE_FRAME_MAX_LEN 128

/*============================================================================
 * Modbus 协议配置
 *============================================================================*/
#define MODBUS_SLAVE_ADDR 0x01
#define MODBUS_RESP_BUF_SIZE 128
#define MODBUS_MAX_REGS_PER_FRAME 125

/*============================================================================
 * 调试日志配置
 *============================================================================*/
#define LOG_ENABLE 1  // 1=启用日志输出, 0=关闭所有日志

#if LOG_ENABLE
#include "app_handles.h"
#define LOG_PRINTF(fmt, ...) bsp_bt401_printf(g_bt401, fmt, ##__VA_ARGS__)
#else
#define LOG_PRINTF(fmt, ...) ((void)0)
#endif

/*============================================================================
 * AT 命令配置
 *============================================================================*/
#define AT_CMD_DELAY_MS 50
#define AT_MAX_PENDING 10
#define AT_DEFAULT_TIMEOUT_MS 2000
#define AT_BT_NAME "LUNAR"
#define AT_BLE_NAME "LUNAR_BLE"

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */
