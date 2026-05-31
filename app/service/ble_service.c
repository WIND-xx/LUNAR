/**
 * @file ble_service.c
 * @brief BLE服务：帧分发 + 看门狗 + AT命令控制
 * @version 3.0
 */

#include "ble_service.h"
#include "FreeRTOS.h"
#include "bt401.h"
#include "core/app_config.h"
#include "led.h"
#include "task.h"
#include <string.h>

#ifndef AT_BT_NAME
#define AT_BT_NAME   "LUNAR"
#endif
#ifndef AT_BLE_NAME
#define AT_BLE_NAME  "LUNAR_BLE"
#endif

typedef struct { uint32_t total, modbus, at, unknown; } frame_stats_t;
static frame_stats_t s_stats;
static ble_frame_cb_t s_frame_cb = NULL;

void ble_service_set_frame_handler(ble_frame_cb_t cb) { s_frame_cb = cb; }

/* ---- BLE帧处理任务 ---- */
static void ble_task(void *arg)
{
    (void)arg;
    frame_t f = {0};
    bt401_init();
    bt_start();

    for (;;) {
#ifdef HAL_IWDG_MODULE_ENABLED
        HAL_IWDG_Refresh(&hiwdg);
#endif
        if (bt401_get_frame(&f, pdMS_TO_TICKS(500))) {
            s_stats.total++;
            bool is_modbus = (f.len >= 5 && f.data[0] == 0x01
                && (f.data[1] == 0x03 || f.data[1] == 0x06 || f.data[1] == 0x10));
            bool is_at = (!is_modbus && f.len >= 2
                && f.data[f.len - 2] == '\r' && f.data[f.len - 1] == '\n');

            if (is_modbus) s_stats.modbus++;
            else if (is_at) s_stats.at++;
            else s_stats.unknown++;

            if (s_frame_cb && (is_modbus || is_at)) {
                s_frame_cb(f.data, f.len, is_modbus);
            }
            f.len = 0;
        }
        if ((s_stats.total % 60000) == 59999) {
            bt401_printf("BLE: F=%lu M=%lu A=%lu U=%lu Stack=%lu\r\n",
                s_stats.total, s_stats.modbus, s_stats.at, s_stats.unknown,
                uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

/* ---- AT命令接口 ---- */
void bt_start(void)
{
    bt401_printf("AT+BD%s\r\n", AT_BT_NAME);  vTaskDelay(pdMS_TO_TICKS(100));
    bt401_printf("AT+BM%s\r\n", AT_BLE_NAME); vTaskDelay(pdMS_TO_TICKS(100));
    bt401_printf("AT+CG01\r\n");              vTaskDelay(pdMS_TO_TICKS(50));
    bt401_printf("AT+CK00\r\n");              vTaskDelay(pdMS_TO_TICKS(50));
    bt401_printf("AT+B200\r\n");              vTaskDelay(pdMS_TO_TICKS(50));
    bt401_printf("AT+CR00\r\n");              vTaskDelay(pdMS_TO_TICKS(50));
    bt401_printf("AT+CN00\r\n");              vTaskDelay(pdMS_TO_TICKS(50));
    ble_mode(BT_MODE_BT);                     vTaskDelay(pdMS_TO_TICKS(50));
}

void music_switch(void)            { bt401_printf("AT+CB\r\n"); }
void music_next(void)              { bt401_printf("AT+CC\r\n"); }
void music_prev(void)              { bt401_printf("AT+CD\r\n"); }

void music_volume_control(volume_dir_t dir)
{
    if (dir == VOL_DIR_UP)   bt401_printf("AT+CE\r\n");
    if (dir == VOL_DIR_DOWN) bt401_printf("AT+CF\r\n");
}
void music_volume_set(uint8_t vol) { if (vol <= 30) bt401_printf("AT+CA%02d\r\n", vol); }

void ble_mode(bt_mode_t mode)
{
    const char *c = NULL;
    switch (mode) {
    case BT_MODE_OFF:   c="AT+CM08\r\n"; led_set_mode(LED_BT,LED_MODE_OFF,0); led_set_mode(LED_MUSIC,LED_MODE_OFF,0); break;
    case BT_MODE_BT:    c="AT+CM01\r\n"; led_set_mode(LED_BT,LED_MODE_ON,0);  break;
    case BT_MODE_MUSIC: c="AT+CM04\r\n"; led_set_mode(LED_MUSIC,LED_MODE_ON,0); break;
    default: return;
    }
    bt401_printf(c);
}
void ble_query(void) { bt401_printf("AT+QM?\r\n"); vTaskDelay(pdMS_TO_TICKS(50)); bt401_printf("AT+TS\r\n"); vTaskDelay(pdMS_TO_TICKS(50)); }

void ble_service_init(void)
{
    xTaskCreate(ble_task, TASK_BLE_NAME, TASK_BLE_STACK, NULL, TASK_BLE_PRIORITY, NULL);
}
