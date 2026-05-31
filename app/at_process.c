/**
 * @file at_process.c
 * @brief AT命令解析（按前缀分流，减少重复字符串扫描）
 * @version 1.1
 */

#include "at_process.h"
#include "led.h"

void decode_at_command(uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) { return; }

    const char *str = (const char *)data;

    // 按命令前缀分流：QM 系列 / TS 系列
    if (strstr(str, "QM")) {
        if (strstr(str, "+00") || strstr(str, "+09")) {
            led_set_mode(LED_BT,    LED_MODE_OFF, 0);
            led_set_mode(LED_MUSIC, LED_MODE_OFF, 0);
        } else if (strstr(str, "+03")) {
            led_set_mode(LED_MUSIC, LED_MODE_ON,  0);
            led_set_mode(LED_BT,    LED_MODE_OFF, 0);
        }
    } else if (strstr(str, "TS")) {
        if (strstr(str, "+00")) {
            led_set_mode(LED_BT, LED_MODE_BLINK, 500);
        } else {
            led_set_mode(LED_BT, LED_MODE_ON, 0);
        }
    }
}
