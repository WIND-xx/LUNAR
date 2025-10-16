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
#include "task.h"
#include <stdint.h>

#define BT_CMD_DELAY_MS 50 // Standard delay between AT commands
#define BT_NAME         "LUNAR"
#define BT_BLE_NAME     "LUNAR_BLE"

/**
 * @brief Initialize and configure bluetooth module
 * @return void
 */
void bt_start(void)
{
    // Set bluetooth device name
    bt401_printf("AT+BD%s\r\n", BT_NAME);
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // Set BLE name
    bt401_printf("AT+BM%s\r\n", BT_BLE_NAME);
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // Enable bluetooth background running
    bt401_printf("AT+CG01\r\n");
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // Disable automatic bluetooth switching
    bt401_printf("AT+CK00\r\n");
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // Disable bluetooth call function
    bt401_printf("AT+B200\r\n");
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // Disable automatic return function
    bt401_printf("AT+CR00\r\n");
    vTaskDelay(BT_CMD_DELAY_MS / portTICK_PERIOD_MS);

    // Set power-on waiting state
    bt401_printf("AT+CP01\r\n");
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
    if (volume <= 30 && volume >= 0) bt401_printf("AT+CA%d\r\n", volume);
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
            break;
        case BTMODE_BT:
            cmd = "AT+CM01\r\n";
            break;
        case BTMODE_MUSIC:
            cmd = "AT+CM04\r\n";
            break;
        default:
            return;
    }

    bt401_printf(cmd);
}
