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
#include <string.h>

void decode_at_command(uint8_t *data, size_t len)
{
    if (strstr(data, "+MF")) {}
}
