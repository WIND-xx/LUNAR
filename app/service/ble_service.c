/**
 * @file ble_service.c
 * @brief BLE服务：帧分发 + 看门狗 + AT命令控制
 * @version 4.0
 */

#include "ble_service.h"
#include "app_config.h"
#include "app_handles.h"
#include "bsp_bt401.h"
#include "bsp_led.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#ifndef AT_BT_NAME
#define AT_BT_NAME "LUNAR"
#endif
#ifndef AT_BLE_NAME
#define AT_BLE_NAME "LUNAR_BLE"
#endif

typedef struct {
    uint32_t total, modbus, at, unknown;
} frame_stats_t;
static frame_stats_t s_stats;
static ble_frame_cb_t s_frame_cb = NULL;

void ble_service_set_frame_handler(ble_frame_cb_t cb)
{
    s_frame_cb = cb;
}

/* ---- BLE帧处理任务 ---- */
static void ble_task(void* arg)
{
    (void)arg;
    bsp_bt401_frame_t f = {0};

    bt_start();

    for (;;) {
#ifdef HAL_IWDG_MODULE_ENABLED
        HAL_IWDG_Refresh(&hiwdg);
#endif
        /* 非阻塞轮询帧 */
        while (bsp_bt401_get_frame(g_bt401, &f) == BSP_OK) {
            s_stats.total++;
            bool is_modbus =
                (f.len >= 5 && f.data[0] == 0x01 && (f.data[1] == 0x03 || f.data[1] == 0x06 || f.data[1] == 0x10));
            bool is_at = (!is_modbus && f.len >= 2 && f.data[f.len - 2] == '\r' && f.data[f.len - 1] == '\n');

            if (is_modbus)
                s_stats.modbus++;
            else if (is_at)
                s_stats.at++;
            else
                s_stats.unknown++;

            if (s_frame_cb && (is_modbus || is_at)) {
                s_frame_cb(f.data, f.len, is_modbus);
            }
            f.len = 0;
        }

        if ((s_stats.total % 60000) == 59999) {
            LOG_PRINTF("BLE: F=%lu M=%lu A=%lu U=%lu Stack=%lu\r\n",
                       s_stats.total, s_stats.modbus, s_stats.at,
                       s_stats.unknown, uxTaskGetStackHighWaterMark(NULL));
        }

        vTaskDelay(pdMS_TO_TICKS(20)); /* 20ms 轮询周期 */
    }
}

/* ---- AT命令接口 ---- */
void bt_start(void)
{
    bsp_bt401_printf(g_bt401, "AT+BD%s\r\n", AT_BT_NAME);
    vTaskDelay(pdMS_TO_TICKS(100));
    bsp_bt401_printf(g_bt401, "AT+BM%s\r\n", AT_BLE_NAME);
    vTaskDelay(pdMS_TO_TICKS(100));
    bsp_bt401_printf(g_bt401, "AT+CG01\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_bt401_printf(g_bt401, "AT+CK00\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_bt401_printf(g_bt401, "AT+B200\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_bt401_printf(g_bt401, "AT+CR00\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_bt401_printf(g_bt401, "AT+CN00\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    ble_mode(BT_MODE_BT);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void music_switch(void)  { bsp_bt401_printf(g_bt401, "AT+CB\r\n"); }
void music_next(void)    { bsp_bt401_printf(g_bt401, "AT+CC\r\n"); }
void music_prev(void)    { bsp_bt401_printf(g_bt401, "AT+CD\r\n"); }

void music_volume_control(volume_dir_t dir)
{
    if (dir == VOL_DIR_UP)
        bsp_bt401_printf(g_bt401, "AT+CE\r\n");
    if (dir == VOL_DIR_DOWN)
        bsp_bt401_printf(g_bt401, "AT+CF\r\n");
}

void music_volume_set(uint8_t vol)
{
    if (vol <= 30)
        bsp_bt401_printf(g_bt401, "AT+CA%02d\r\n", vol);
}

void ble_mode(bt_mode_t mode)
{
    const char* c = NULL;
    switch (mode) {
        case BT_MODE_OFF:
            c = "AT+CM08\r\n";
            bsp_led_set_mode(g_led, BSP_LED_BT, BSP_LED_MODE_OFF, 0);
            bsp_led_set_mode(g_led, BSP_LED_MUSIC, BSP_LED_MODE_OFF, 0);
            break;
        case BT_MODE_BT:
            c = "AT+CM01\r\n";
            bsp_led_set_mode(g_led, BSP_LED_BT, BSP_LED_MODE_ON, 0);
            break;
        case BT_MODE_MUSIC:
            c = "AT+CM04\r\n";
            bsp_led_set_mode(g_led, BSP_LED_MUSIC, BSP_LED_MODE_ON, 0);
            break;
        default:
            return;
    }
    bsp_bt401_printf(g_bt401, c);
}

void ble_query(void)
{
    bsp_bt401_printf(g_bt401, "AT+QM?\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_bt401_printf(g_bt401, "AT+TS\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
}

void ble_service_init(void)
{
    xTaskCreate(ble_task, TASK_BLE_NAME, TASK_BLE_STACK, NULL, TASK_BLE_PRIORITY, NULL);
}
