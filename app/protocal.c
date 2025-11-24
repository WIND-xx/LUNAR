/**
 * @file protocal.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-11-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "protocal.h"
#include "BufferProcess.h"
#include "FreeRTOS.h"
#include "alarm.h"
#include "bsp_rtc.h"
#include "bt401.h"
#include "crc16.h"
#include "task.h"
#include <stdbool.h>

extern void     do_reg_change_actions(RegisterID reg, uint16_t value);
static uint16_t g_registers[REG_COUNT] = {0};

// -------------------------- 辅助函数 --------------------------
static uint16_t _register_get_value(RegisterID reg_id)
{
    if (reg_id >= REG_COUNT) return 0xFFFF;
    return g_registers[reg_id];
}

static bool _register_set_normal(RegisterID reg_id, uint16_t value)
{
    if (reg_id >= REG_COUNT) return false;

    switch (reg_id)
    {
        case REG_HEATING_LEVEL:
            if (value > 2) return false;
            break;
        case REG_HEATING_TIMER:
            if (value > 720) return false;
            break;
        case REG_HEATING_STATUS:
            if (value > 1) return false;
            break;
        default:
            break;
    }

    g_registers[reg_id] = value;
    return true;
}

static bool _process_special_regs(uint16_t start_addr, uint16_t reg_count, const uint8_t *write_data)
{
    for (uint16_t i = 0; i < reg_count; i++)
    {
        RegisterID curr_reg = (RegisterID) (start_addr + i);
        uint16_t   write_val = (write_data[i * 2] << 8) | write_data[i * 2 + 1];

        switch (curr_reg)
        {
            case REG_UTC_TIMESTAMP_HIGH: {
                if (start_addr + i + 1 >= start_addr + reg_count || (REG_UTC_TIMESTAMP_LOW - start_addr) >= reg_count)
                    return false;

                uint16_t utc_low = (write_data[(REG_UTC_TIMESTAMP_LOW - start_addr) * 2] << 8) |
                                   write_data[(REG_UTC_TIMESTAMP_LOW - start_addr) * 2 + 1];
                uint32_t utc_full = ((uint32_t) write_val << 16) | utc_low;

                if (RTC_SetUTC(utc_full) != 0) return false;
                g_registers[REG_UTC_TIMESTAMP_HIGH] = write_val;
                g_registers[REG_UTC_TIMESTAMP_LOW] = utc_low;
                break;
            }
            case REG_UTC_TIMESTAMP_LOW:
                break;

            case REG_ALARM_SET_HIGH:
                if (alarm_handle_modbus_write(REG_ALARM_SET_HIGH, write_val) != ALARM_OK) return false;
                g_registers[REG_ALARM_SET_HIGH] = write_val;
                break;

            case REG_ALARM_SET_LOW:
                if (alarm_handle_modbus_write(REG_ALARM_SET_LOW, write_val) != ALARM_OK) return false;
                g_registers[REG_ALARM_SET_LOW] = write_val;
                break;

            case REG_DELETE_ALARM:
                if (alarm_handle_modbus_write(REG_DELETE_ALARM, write_val) != ALARM_OK) return false;
                g_registers[REG_DELETE_ALARM] = write_val;
                break;

            case REG_EXECUTE_SHORTCUT:
                if (write_val != 1 && write_val != 2) return false;
                break;

            default:
                return false;
        }
    }
    return true;
}

static void _send_modbus_response(uint8_t *response_buf, uint16_t response_len)
{
    uint16_t crc = Modbus_CRC16(response_buf, response_len);
    response_buf[response_len] = crc & 0xFF;
    response_buf[response_len + 1] = (crc >> 8) & 0xFF;
    bt401_sendbytes(response_buf, response_len + 2);
}

// -------------------------- Modbus帧处理函数 --------------------------
bool decode_protocal(uint8_t *data, size_t len)
{
    uint8_t  tx_frame[MODBUS_FRAME_MAX_LEN] = {0};
    uint16_t tx_len = 0;

    if (len < 2) return false;

    uint8_t        slave_addr = data[0];
    uint8_t        func_code = data[1];
    uint16_t       reg_addr = 0, reg_count = 0;
    const uint8_t *write_data = NULL;

    tx_frame[0] = slave_addr;
    tx_frame[1] = func_code;
    tx_len = 2;

    switch (func_code)
    {
        case 0x03: // 读保持寄存器
            if (len >= 8)
            {
                reg_addr = (data[2] << 8) | data[3];
                reg_count = (data[4] << 8) | data[5];

                if (reg_addr < REG_COUNT && reg_addr + reg_count <= REG_COUNT)
                {
                    tx_frame[2] = reg_count * 2;
                    tx_len = 3;

                    for (uint16_t i = 0; i < reg_count; i++)
                    {
                        uint16_t reg_val = _register_get_value((RegisterID) (reg_addr + i));
                        if (reg_val == 0xFFFF) { return false; }
                        tx_frame[tx_len++] = (reg_val >> 8) & 0xFF;
                        tx_frame[tx_len++] = reg_val & 0xFF;
                    }
                    _send_modbus_response(tx_frame, tx_len);
                    return true;
                }
            }
            break;

        case 0x10: // 写多个保持寄存器
            if (len >= 9)
            {
                reg_addr = (data[2] << 8) | data[3];
                reg_count = (data[4] << 8) | data[5];
                uint8_t byte_count = data[6];
                write_data = &data[7];

                if (byte_count == reg_count * 2 && reg_addr < REG_COUNT && reg_addr + reg_count <= REG_COUNT &&
                    7u + byte_count <= len)
                {

                    bool     write_success = true;
                    uint16_t special_reg_count = 0;

                    if (reg_addr <= REG_EXECUTE_SHORTCUT)
                    {
                        special_reg_count = (reg_addr + reg_count > REG_EXECUTE_SHORTCUT + 1)
                                                ? (REG_EXECUTE_SHORTCUT + 1 - reg_addr)
                                                : reg_count;
                        if (!_process_special_regs(reg_addr, special_reg_count, write_data)) write_success = false;
                    }

                    if (write_success && (reg_addr + reg_count > REG_EXECUTE_SHORTCUT + 1))
                    {
                        uint16_t normal_reg_start =
                            (reg_addr > REG_EXECUTE_SHORTCUT) ? reg_addr : (REG_EXECUTE_SHORTCUT + 1);
                        uint16_t       normal_reg_count = reg_count - special_reg_count;
                        const uint8_t *normal_write_data = write_data + special_reg_count * 2;

                        for (uint16_t i = 0; i < normal_reg_count; i++)
                        {
                            RegisterID curr_reg = (RegisterID) (normal_reg_start + i);
                            uint16_t   write_val = (normal_write_data[i * 2] << 8) | normal_write_data[i * 2 + 1];
                            if (!_register_set_normal(curr_reg, write_val))
                            {
                                write_success = false;
                                break;
                            }
                            do_reg_change_actions(curr_reg, write_val);
                        }
                    }

                    if (write_success)
                    {
                        tx_frame[2] = (reg_addr >> 8) & 0xFF;
                        tx_frame[3] = reg_addr & 0xFF;
                        tx_frame[4] = (reg_count >> 8) & 0xFF;
                        tx_frame[5] = reg_count & 0xFF;
                        tx_len = 6;
                        _send_modbus_response(tx_frame, tx_len);
                        return true;
                    }
                }
            }
            break;

        default:
            // 对于不支持的功能码，什么都不做
            break;
    }
    return false;
}
