/**
 * @file at_process.h
 * @brief AT命令解析器（前缀匹配 + 状态机）
 * @version 2.0
 */

#ifndef AT_PARSER_H
#define AT_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AT_CMD_NONE, AT_CMD_QM_OFF, AT_CMD_QM_MUSIC, AT_CMD_TS_BLINK, AT_CMD_TS_ON, AT_CMD_UNKNOWN
} at_parsed_cmd_t;

typedef struct { at_parsed_cmd_t cmd; const char *raw; uint16_t raw_len; } at_parse_result_t;

at_parse_result_t at_parse_frame(const uint8_t *data, uint16_t len);
void decode_at_command(uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif

