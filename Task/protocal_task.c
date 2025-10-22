#include "protocal_task.h"
#include "BufferProcess.h"
#include "FreeRTOS.h"
#include "alarm.h"
#include "bsp_rtc.h"
#include "bt401.h"
#include "crc16.h"
#include "task.h"

// Modbus 核心常量
#define MODBUS_FUNC_READ_HOLDING_REG   0x03
#define MODBUS_FUNC_WRITE_MULTIPLE_REG 0x10
#define MODBUS_EXCEPTION_ILLEGAL_FUNC  0x01
#define MODBUS_EXCEPTION_ILLEGAL_ADDR  0x02
#define MODBUS_EXCEPTION_ILLEGAL_VAL   0x03

extern void     do_reg_change_actions(RegisterID reg, uint16_t value);
static uint16_t g_registers[REG_COUNT] = {0};

// -------------------------- 模块化辅助函数 --------------------------
static uint16_t _register_get_value(RegisterID reg_id)
{
    if (reg_id >= REG_COUNT) return 0xFFFF;
    if (reg_id <= REG_EXECUTE_SHORTCUT) return 0xFFFF; // 只写寄存器禁止读
    return g_registers[reg_id];
}

static bool _register_set_normal(RegisterID reg_id, uint16_t value)
{
    if (reg_id >= REG_COUNT) return false;
    if (reg_id <= REG_EXECUTE_SHORTCUT) return false; // 特殊寄存器需单独处理

    switch (reg_id)
    {
        case REG_HEATING_LEVEL:
            if (value > 2) return false; // 修正原注释（1-2档，原注释1-5可能笔误）
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
                // 检查低位是否在本次写入范围内
                if (start_addr + i + 1 >= start_addr + reg_count || (REG_UTC_TIMESTAMP_LOW - start_addr) >= reg_count)
                {
                    return false; // 高低位未同时写入
                }
                uint16_t utc_low = (write_data[(REG_UTC_TIMESTAMP_LOW - start_addr) * 2] << 8) |
                                   write_data[(REG_UTC_TIMESTAMP_LOW - start_addr) * 2 + 1];
                uint32_t utc_full = ((uint32_t) write_val << 16) | utc_low;
                if (RTC_SetUTC(utc_full) != 0) return false;
                g_registers[REG_UTC_TIMESTAMP_HIGH] = write_val;
                g_registers[REG_UTC_TIMESTAMP_LOW] = utc_low;
                break;
            }
            case REG_UTC_TIMESTAMP_LOW:
                break; // 已在高位处理

            case REG_ALARM_SET_HIGH:
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
                // 快捷键逻辑（根据实际需求补充）
                // g_registers[REG_HEATING_STATUS] = 1;
                // g_registers[REG_HEATING_LEVEL] = (write_val == 1) ? g_registers[REG_SHORTCUT_KEY1] :
                // g_registers[REG_SHORTCUT_KEY2];
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

// -------------------------- 核心：Modbus消息处理任务 --------------------------
void vProtocalTask(void *pvParameters)
{
    (void) pvParameters;

    uint8_t  modbus_rx_frame[MODBUS_FRAME_MAX_LEN] = {0};
    uint8_t  modbus_tx_frame[MODBUS_FRAME_MAX_LEN] = {0};
    uint16_t tx_len = 0;
    size_t   frame_len = 0; // 实际接收的帧长度

    for (;;)
    {
        // 通过封装函数从队列获取Modbus帧（阻塞等待）
        frame_len = get_data_frame(modbus_rx_frame, MODBUS_FRAME_MAX_LEN);
        if (frame_len == 0)
        {
            // 未获取到有效帧，继续等待
            continue;
        }

        // 帧长度最小检查（至少包含：地址1 + 功能码1 → 2字节）
        if (frame_len < 2)
        {
            memset(modbus_rx_frame, 0, MODBUS_FRAME_MAX_LEN);
            continue;
        }

        // 解析帧头核心字段（带边界检查）
        uint8_t        slave_addr = modbus_rx_frame[0];
        uint8_t        func_code = modbus_rx_frame[1];
        uint16_t       reg_addr = 0;
        uint16_t       reg_count = 0;
        const uint8_t *write_data = NULL;

        // 初始化响应帧基础信息
        modbus_tx_frame[0] = slave_addr;
        modbus_tx_frame[1] = func_code;
        tx_len = 2;

        // -------------------------- 功能码处理 --------------------------
        switch (func_code)
        {
            // 读保持寄存器（0x03）
            case MODBUS_FUNC_READ_HOLDING_REG:
                // 0x03帧结构：地址1 + 功能1 + 起始地址2 + 数量2 + CRC2 → 最小8字节
                if (frame_len < 8)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                reg_addr = (modbus_rx_frame[2] << 8) | modbus_rx_frame[3];
                reg_count = (modbus_rx_frame[4] << 8) | modbus_rx_frame[5];

                // 地址范围检查
                if (reg_addr >= REG_COUNT || reg_addr + reg_count > REG_COUNT)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_ADDR;
                    tx_len = 3;
                    break;
                }

                // 读权限检查（禁止读只写寄存器）
                if (reg_addr <= REG_EXECUTE_SHORTCUT)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                // 填充响应数据
                modbus_tx_frame[2] = reg_count * 2; // 数据总字节数
                tx_len = 3;
                bool read_valid = true;

                for (uint16_t i = 0; i < reg_count; i++)
                {
                    uint16_t reg_val = _register_get_value((RegisterID) (reg_addr + i));
                    if (reg_val == 0xFFFF)
                    {
                        read_valid = false;
                        break;
                    }
                    modbus_tx_frame[tx_len++] = (reg_val >> 8) & 0xFF;
                    modbus_tx_frame[tx_len++] = reg_val & 0xFF;
                }

                if (!read_valid)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                }
                break;

            // 写多个寄存器（0x10）
            case MODBUS_FUNC_WRITE_MULTIPLE_REG:
                // 0x10帧结构：地址1 + 功能1 + 起始地址2 + 数量2 + 字节数1 + 数据n + CRC2 → 最小9字节
                if (frame_len < 9)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                reg_addr = (modbus_rx_frame[2] << 8) | modbus_rx_frame[3];
                reg_count = (modbus_rx_frame[4] << 8) | modbus_rx_frame[5];
                uint8_t byte_count = modbus_rx_frame[6];
                write_data = &modbus_rx_frame[7];

                // 数据长度合法性检查（字节数必须=寄存器数×2）
                if (byte_count != reg_count * 2)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                // 地址范围检查（防止越界）
                if (reg_addr >= REG_COUNT || reg_addr + reg_count > REG_COUNT)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_ADDR;
                    tx_len = 3;
                    break;
                }

                // 检查数据区是否超出帧长度（防止访问越界）
                if (7 + byte_count > frame_len)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                // 处理特殊寄存器和普通寄存器
                bool     write_success = true;
                uint16_t special_reg_count = 0;

                if (reg_addr <= REG_EXECUTE_SHORTCUT)
                {
                    special_reg_count = (reg_addr + reg_count > REG_EXECUTE_SHORTCUT + 1)
                                            ? (REG_EXECUTE_SHORTCUT + 1 - reg_addr)
                                            : reg_count;
                    if (!_process_special_regs(reg_addr, special_reg_count, write_data)) { write_success = false; }
                }

                // 处理普通寄存器
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

                // 处理写入结果
                if (!write_success)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                // 写入成功响应：起始地址+寄存器数量
                modbus_tx_frame[2] = (reg_addr >> 8) & 0xFF;
                modbus_tx_frame[3] = reg_addr & 0xFF;
                modbus_tx_frame[4] = (reg_count >> 8) & 0xFF;
                modbus_tx_frame[5] = reg_count & 0xFF;
                tx_len = 6;
                break;

            // 非法功能码
            default:
                modbus_tx_frame[1] |= 0x80;
                modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_FUNC;
                tx_len = 3;
                break;
        }

        // 发送响应并清空缓冲区
        _send_modbus_response(modbus_tx_frame, tx_len);
        memset(modbus_rx_frame, 0, MODBUS_FRAME_MAX_LEN);
        memset(modbus_tx_frame, 0, MODBUS_FRAME_MAX_LEN);
    }
}

void protocal_task_init(void)
{
    xTaskCreate(vProtocalTask, "protocalTask", 512, NULL, 4, NULL);
}
