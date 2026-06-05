/**
 * @file at_process.c
 * @brief AT命令解析器（查表驱动 + 前缀匹配）
 * @version 2.0
 */

#include "at_parser.h"
#include "led.h"
#include <string.h>

/*============================================================================
 * 命令查找表
 *============================================================================*/
typedef struct {
    const char* pattern;
    at_parsed_cmd_t cmd;
} at_cmd_entry_t;

static const at_cmd_entry_t at_cmd_table[] = {
    {"QM+00", AT_CMD_QM_OFF},
    {"QM+09", AT_CMD_QM_OFF},
    {"QM+03", AT_CMD_QM_MUSIC},
    {"TS+00", AT_CMD_TS_BLINK},
};

#define AT_CMD_TABLE_SIZE (sizeof(at_cmd_table) / sizeof(at_cmd_table[0]))

/*============================================================================
 * 公共 API
 *============================================================================*/

at_parse_result_t at_parse_frame(const uint8_t* data, uint16_t len)
{
    at_parse_result_t result = {AT_CMD_NONE, (const char*)data, len};

    if (!data || len < 4)
        return result;

    /* 查表匹配 */
    for (uint8_t i = 0; i < AT_CMD_TABLE_SIZE; i++) {
        size_t pat_len = strlen(at_cmd_table[i].pattern);
        if (pat_len <= len && memcmp(data, at_cmd_table[i].pattern, pat_len) == 0) {
            result.cmd = at_cmd_table[i].cmd;
            return result;
        }
    }

    /* 前缀匹配 TS 系列（TS+XX → 蓝牙常亮） */
    if (len >= 2 && data[0] == 'T' && data[1] == 'S') {
        result.cmd = AT_CMD_TS_ON;
        return result;
    }

    result.cmd = AT_CMD_UNKNOWN;
    return result;
}

/*============================================================================
 * 兼容旧接口
 *============================================================================*/

void decode_at_command(uint8_t* data, size_t len)
{
    if (!data || len < 2)
        return;

    /* 验证 \r\n 结尾 */
    if (data[len - 2] != '\r' || data[len - 1] != '\n')
        return;

    /* 去掉 \r\n，解析内容 */
    at_parse_result_t result = at_parse_frame(data, (uint16_t)(len - 2));

    switch (result.cmd) {
        case AT_CMD_QM_OFF:
            led_set_mode(LED_BT, LED_MODE_OFF, 0);
            led_set_mode(LED_MUSIC, LED_MODE_OFF, 0);
            break;
        case AT_CMD_QM_MUSIC:
            led_set_mode(LED_MUSIC, LED_MODE_ON, 0);
            led_set_mode(LED_BT, LED_MODE_OFF, 0);
            break;
        case AT_CMD_TS_BLINK:
            led_set_mode(LED_BT, LED_MODE_BLINK, 500);
            break;
        case AT_CMD_TS_ON:
            led_set_mode(LED_BT, LED_MODE_ON, 0);
            break;
        default:
            break;
    }
}
