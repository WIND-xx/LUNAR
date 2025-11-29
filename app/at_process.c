/**
 * @file at_process.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-11-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "at_process.h"

#include "led.h"
void decode_at_command(uint8_t *data, size_t len)
{
    // 参数有效性检查
    if (data == NULL || len == 0) { return; }

    // 缓存字符串指针，避免重复转换
    const char *str_data = (const char *) data;

    // 检查QM相关命令
    if (strstr(str_data, "QM+00") || strstr(str_data, "QM+09"))
    {
        led_set_mode(LED_BT, LED_MODE_OFF, 0);
        led_set_mode(LED_MUSIC, LED_MODE_OFF, 0);
    }
    else if (strstr(str_data, "QM+03"))
    {
        led_set_mode(LED_MUSIC, LED_MODE_ON, 0);
        led_set_mode(LED_BT, LED_MODE_OFF, 0);
    }
    // 检查TS相关命令
    else
    {
        const char *ts_ptr = strstr(str_data, "TS");
        if (ts_ptr != NULL)
        {
            if (strstr(ts_ptr, "TS+00")) { led_set_mode(LED_BT, LED_MODE_BLINK, 500); }
            else { led_set_mode(LED_BT, LED_MODE_ON, 0); }
        }
    }
}
