/**
 * @file protocal.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-11-30
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
#include <stdint.h>

#define MODBUS_FUNC_READ_HOLDING   0x03
#define MODBUS_FUNC_WRITE_MULTIPLE 0x10
#define MODBUS_SLAVE_ADDR          0x01
#define MODBUS_MIN_FRAME_LEN       4
#define MODBUS_READ_FRAME_MIN_LEN  8
#define MODBUS_WRITE_FRAME_MIN_LEN 9

extern void     do_reg_change_actions(RegisterID reg, uint16_t value);
static uint16_t g_registers[REG_COUNT] = {0};

static uint16_t _register_get_value(RegisterID reg_id);
static bool     _register_set_normal(RegisterID reg_id, uint16_t value);
static bool     _process_special_regs(uint16_t start_reg, uint16_t reg_num, const uint8_t *write_buf);
static void     _send_modbus_response(uint8_t *response_buf, uint16_t response_len);
static bool     validate_register_range(uint16_t start_reg, uint16_t reg_num);

static uint16_t _register_get_value(RegisterID reg_id)
{
    uint16_t value = 0xFFFF;
    taskENTER_CRITICAL();
    if (reg_id < REG_COUNT) value = g_registers[reg_id];
    taskEXIT_CRITICAL();
    return value;
}

static bool _register_set_normal(RegisterID reg_id, uint16_t value)
{
    bool ret = false;
    taskENTER_CRITICAL();
    if (reg_id >= REG_COUNT)
    {
        taskEXIT_CRITICAL();
        return ret;
    }

    switch (reg_id)
    {
        case REG_HEATING_LEVEL:
            ret = (value <= 2);
            break;
        case REG_HEATING_TIMER:
            ret = (value <= 720);
            break;
        case REG_HEATING_STATUS:
            ret = (value <= 1);
            break;
        default:
            ret = true;
    }

    if (ret) g_registers[reg_id] = value;
    taskEXIT_CRITICAL();
    return ret;
}

static bool validate_register_range(uint16_t start_reg, uint16_t reg_num)
{
    return (start_reg < REG_COUNT) && ((start_reg + reg_num) <= REG_COUNT) && (reg_num > 0);
}

static bool _process_special_regs(uint16_t start_reg, uint16_t reg_num, const uint8_t *write_buf)
{
    for (uint16_t i = 0; i < reg_num; i++)
    {
        RegisterID curr_reg = (RegisterID) (start_reg + i);
        uint16_t   write_val = (write_buf[i * 2] << 8) | write_buf[i * 2 + 1];

        switch (curr_reg)
        {
            case REG_UTC_TIMESTAMP_HIGH:
                if ((i + 1) >= reg_num) return false;
                uint16_t utc_low = (write_buf[(i + 1) * 2] << 8) | write_buf[(i + 1) * 2 + 1];
                uint32_t utc_full = ((uint32_t) write_val << 16) | utc_low;
                if (RTC_SetUTC(utc_full) != 0) return false;
                g_registers[REG_UTC_TIMESTAMP_HIGH] = write_val;
                g_registers[REG_UTC_TIMESTAMP_LOW] = utc_low;
                i++;
                break;

            case REG_UTC_TIMESTAMP_LOW:
                return false;

            case REG_ALARM_SET_HIGH:
            case REG_ALARM_SET_LOW:
            case REG_DELETE_ALARM:
                if (alarm_handle_modbus_write(curr_reg, write_val) != ALARM_OK) return false;
                g_registers[curr_reg] = write_val;
                break;

            case REG_EXECUTE_SHORTCUT:
                if ((write_val != 1) && (write_val != 2)) return false;
                g_registers[curr_reg] = write_val;
                break;

            default:
                return false;
        }
    }
    return true;
}

static void _send_modbus_response(uint8_t *response_buf, uint16_t response_len)
{
    if (!response_buf || !response_len || (response_len + 2) > MODBUS_FRAME_MAX_LEN) return;
    uint16_t crc = Modbus_CRC16(response_buf, response_len);
    response_buf[response_len] = crc & 0xFF;
    response_buf[response_len + 1] = (crc >> 8) & 0xFF;
    bt401_sendbytes(response_buf, response_len + 2);
}

bool decode_protocal(const uint8_t *data, size_t len)
{
    if (len < MODBUS_MIN_FRAME_LEN) return false;
    uint8_t slave_addr = data[0];
    uint8_t func_code = data[1];
    if (slave_addr != MODBUS_SLAVE_ADDR) return false;

    uint8_t response_buf[MODBUS_FRAME_MAX_LEN] = {0};
    response_buf[0] = slave_addr;
    response_buf[1] = func_code;
    uint16_t response_len = 2;

    switch (func_code)
    {
        case MODBUS_FUNC_READ_HOLDING:
            if (len < MODBUS_READ_FRAME_MIN_LEN) return false;
            uint16_t start_reg = (data[2] << 8) | data[3];
            uint16_t reg_num = (data[4] << 8) | data[5];
            if (!validate_register_range(start_reg, reg_num)) return false;
            response_buf[2] = (uint8_t) (reg_num * 2);
            response_len = 3;
            for (uint16_t i = 0; i < reg_num; i++)
            {
                RegisterID curr_reg = (RegisterID) (start_reg + i);
                uint16_t   reg_val = _register_get_value(curr_reg);
                if (reg_val == 0xFFFF) return false;
                if ((response_len + 2) > MODBUS_FRAME_MAX_LEN) return false;
                response_buf[response_len++] = (reg_val >> 8) & 0xFF;
                response_buf[response_len++] = reg_val & 0xFF;
            }
            _send_modbus_response(response_buf, response_len);
            return true;

        case MODBUS_FUNC_WRITE_MULTIPLE:
            if (len < MODBUS_WRITE_FRAME_MIN_LEN) return false;
            start_reg = (data[2] << 8) | data[3];
            reg_num = (data[4] << 8) | data[5];
            uint8_t        byte_count = data[6];
            const uint8_t *write_buf = &data[7];
            if ((byte_count != (reg_num * 2)) || !validate_register_range(start_reg, reg_num) ||
                (7U + byte_count > len))
                return false;

            bool     write_success = true;
            uint16_t special_reg_num = 0;

            if (start_reg <= REG_EXECUTE_SHORTCUT)
            {
                special_reg_num =
                    (start_reg + reg_num > REG_EXECUTE_SHORTCUT + 1) ? (REG_EXECUTE_SHORTCUT + 1 - start_reg) : reg_num;
                taskENTER_CRITICAL();
                write_success = _process_special_regs(start_reg, special_reg_num, write_buf);
                taskEXIT_CRITICAL();
                if (!write_success) return false;
            }

            if (write_success && (start_reg + reg_num > REG_EXECUTE_SHORTCUT + 1))
            {
                uint16_t normal_reg_start = (start_reg > REG_EXECUTE_SHORTCUT) ? start_reg : (REG_EXECUTE_SHORTCUT + 1);
                uint16_t normal_reg_num = reg_num - special_reg_num;
                const uint8_t *normal_write_buf = write_buf + (special_reg_num * 2);

                for (uint16_t i = 0; i < normal_reg_num; i++)
                {
                    RegisterID curr_reg = (RegisterID) (normal_reg_start + i);
                    uint16_t   write_val = (normal_write_buf[i * 2] << 8) | normal_write_buf[i * 2 + 1];
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
                response_buf[2] = (start_reg >> 8) & 0xFF;
                response_buf[3] = start_reg & 0xFF;
                response_buf[4] = (reg_num >> 8) & 0xFF;
                response_buf[5] = reg_num & 0xFF;
                response_len = 6;
                _send_modbus_response(response_buf, response_len);
                return true;
            }
            return false;

        default:
            return false;
    }
}

void protocal_uplode_heat(void)
{
    uint8_t response_buf[MODBUS_FRAME_MAX_LEN] = {0};
    response_buf[0] = MODBUS_SLAVE_ADDR;
    response_buf[1] = MODBUS_FUNC_READ_HOLDING;
    response_buf[2] = 6;
    uint16_t response_len = 3;

    for (uint16_t i = 0; i < 3; i++)
    {
        uint16_t reg_val;
        taskENTER_CRITICAL();
        reg_val = g_registers[REG_HEATING_STATUS + i];
        taskEXIT_CRITICAL();
        response_buf[response_len++] = (reg_val >> 8) & 0xFF;
        response_buf[response_len++] = reg_val & 0xFF;
    }

    _send_modbus_response(response_buf, response_len);
}
