/**
 * @file AT_ctrl.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief Bluetooth control interface implementation
 * @version 0.2
 * @date 2025-10-16
 *
 * @copyright Copyright (c) 2025
 */

#include "at_ctrl.h"
#include "FreeRTOS.h"
#include "bt401.h"
#include "led.h"
#include "projdefs.h"
#include "task.h"
#include <stdint.h>

#define BT_CMD_DELAY_MS 50 // Standard delay between AT commands
#define BT_NAME         "LUNAR"
#define BT_BLE_NAME     "LUNAR_BLE"
#define DEVICES_TYPE_U  1
// #define DEVICES_TYPE_N  1

/**
 * @brief Initialize and configure bluetooth module
 * @return void
 */
/**
 * @brief 初始化并配置蓝牙模块
 * @return void
 */
void bt_start(void)
{
    // 设置蓝牙设备名称为 "LUNAR"
    bt401_printf("AT+BD%s\r\n", BT_NAME);
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // 设置BLE广播名称为 "LUNAR_BLE"
    bt401_printf("AT+BM%s\r\n", BT_BLE_NAME);
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // 启用蓝牙后台运行模式（不主动连接）
    bt401_printf("AT+CG01\r\n");
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // 禁止自动切换蓝牙模式
    bt401_printf("AT+CK00\r\n");
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // 禁用蓝牙通话功能
    bt401_printf("AT+B200\r\n");
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // 禁止自动回连功能
    bt401_printf("AT+CR00\r\n");
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);
#ifdef DEVICES_TYPE_N
    // 设置上电等待状态（等待主控进一步指令）
    bt401_printf("AT+CP01\r\n");
#endif // DEBUG
#ifdef DEVICES_TYPE_U
    bt401_printf("AT+CN00\r\n"); // 关闭提示音
    vTaskDelay(pdMS_TO_TICKS(50));
    ble_mode(BTMODE_BT); // 直接进入蓝牙模式
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
}

/**
 * @brief Toggle music play/pause
 */
void music_switch(void)
{
    bt401_printf("AT+CB\r\n");
}

/**
 * @brief Play next track
 */
void music_next(void)
{
    bt401_printf("AT+CC\r\n");
}

/**
 * @brief Play previous track
 */
void music_prev(void)
{
    bt401_printf("AT+CD\r\n");
}

/**
 * @brief Control music volume
 * @param ctrl VOLUME_UP or VOLUME_DOWN
 */
void music_volume_control(VOLUME_ENUM ctrl)
{
    if (ctrl != VOLUME_UP && ctrl != VOLUME_DOWN) { return; }

    bt401_printf("AT+C%c\r\n", (ctrl == VOLUME_UP) ? 'E' : 'F');
}
void music_volume_set(uint8_t volume)
{
    if (volume <= 30) bt401_printf("AT+CA%02d\r\n", volume);
}
/**
 * @brief Set bluetooth mode
 * @param mode BTMODE_OFF, BTMODE_BT, or BTMODE_MUSIC
 */
void ble_mode(BTMODE_ENUM mode)
{
    const char *cmd = NULL;

    switch (mode)
    {
        case BTMODE_OFF:
            cmd = "AT+CM08\r\n";
            led_set_mode(LED_BT, LED_MODE_OFF, 0);
            led_set_mode(LED_MUSIC, LED_MODE_OFF, 0);
            break;
        case BTMODE_BT:
            cmd = "AT+CM01\r\n";
            led_set_mode(LED_BT, LED_MODE_ON, 0);
            break;
        case BTMODE_MUSIC:
            cmd = "AT+CM04\r\n";
            led_set_mode(LED_MUSIC, LED_MODE_ON, 0);
            break;
        default:
            return;
    }

    bt401_printf(cmd);
}
void ble_query(void)
{
    bt401_printf("AT+QM?\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    bt401_printf("AT+TS\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
}
