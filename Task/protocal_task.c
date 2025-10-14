#include "protocal_task.h"
#include "BufferProcess.h"
#include "FreeRTOS.h"
// #include "alarm.h" // 闹钟处理
#include "bt401.h"
#include "crc16.h"
#include "rtc.h" // UTC时间处理
#include "task.h"

#include "alarm.h"

// Modbus 核心常量（仅保留需用到的功能码/异常码）
#define MODBUS_FUNC_READ_HOLDING_REG   0x03 // 读保持寄存器
#define MODBUS_FUNC_WRITE_MULTIPLE_REG 0x10 // 写多个寄存器
#define MODBUS_EXCEPTION_ILLEGAL_FUNC  0x01 // 非法功能码
#define MODBUS_EXCEPTION_ILLEGAL_ADDR  0x02 // 非法地址
#define MODBUS_EXCEPTION_ILLEGAL_VAL   0x03 // 非法值

extern void do_reg_change_actions(RegisterID reg, uint16_t value);
// 全局寄存器存储（私有，仅通过内部接口访问）
static uint16_t g_registers[REG_COUNT] = {0};

// -------------------------- 模块化辅助函数 --------------------------
// 1. 寄存器读操作（统一封装，含读权限校验）
static uint16_t _register_get_value(RegisterID reg_id)
{
    // 合法性检查：寄存器ID超出范围
    if (reg_id >= REG_COUNT) return 0xFFFF; // 自定义非法值标识

    // 读权限检查：REG_EXECUTE_SHORTCUT及之前为只写寄存器，禁止读
    if (reg_id <= REG_EXECUTE_SHORTCUT) return 0xFFFF;

    return g_registers[reg_id];
}

// 2. 普通寄存器写操作（不含特殊逻辑，含值范围校验）
static bool _register_set_normal(RegisterID reg_id, uint16_t value)
{
    // 合法性检查：寄存器ID超出范围
    if (reg_id >= REG_COUNT) return false;

    // 特殊只写寄存器禁止通过"普通写"操作处理（需单独逻辑）
    if (reg_id <= REG_EXECUTE_SHORTCUT) return false;

    // 值范围校验（根据寄存器功能限制）
    switch (reg_id)
    {
        case REG_HEATING_LEVEL:
            if (value < 0 || value > 2) return false; // 热敷档位1-5
            break;
        case REG_HEATING_TIMER:
            if (value > 0xffff) return false; // 定时0-120分钟（0=无定时）
            break;
        case REG_HEATING_STATUS:
            if (value > 1) return false; // 状态0=关闭，1=开启
            break;
        default:
            break; // 其他读写寄存器无特殊范围限制
    }

    g_registers[reg_id] = value;
    return true;
}

// 3. 特殊只写寄存器批量处理（针对0x10功能码，处理高低位关联逻辑）
static bool _process_special_regs(uint16_t start_addr, uint16_t reg_count, const uint8_t *write_data)
{
    // 遍历待写入的特殊寄存器
    for (uint16_t i = 0; i < reg_count; i++)
    {
        RegisterID curr_reg = (RegisterID) (start_addr + i);
        uint16_t   write_val = (write_data[i * 2] << 8) | write_data[i * 2 + 1];

        switch (curr_reg)
        {
            // 情况1：UTC时间戳（高低位必定同时写入）
            case REG_UTC_TIMESTAMP_HIGH: {
                // 直接从本次写入数据中获取高低位
                uint16_t utc_high = write_val;
                uint16_t utc_low = (write_data[(REG_UTC_TIMESTAMP_LOW - start_addr) * 2] << 8) |
                                   write_data[(REG_UTC_TIMESTAMP_LOW - start_addr) * 2 + 1];
                uint32_t utc_full = ((uint32_t) utc_high << 16) | utc_low;
                if (RTC_SetUTC(utc_full) != 0) return false; // 硬件写入失败
                g_registers[REG_UTC_TIMESTAMP_HIGH] = utc_high;
                g_registers[REG_UTC_TIMESTAMP_LOW] = utc_low;
                break;
            }
            case REG_UTC_TIMESTAMP_LOW:
                // 已在REG_UTC_TIMESTAMP_HIGH处理，无需重复处理
                break;

                break;

            // 情况2：闹钟设置（需高低位同时写入）
            case REG_ALARM_SET_HIGH:
                // 缓存高位值，等待低位写入
                g_registers[REG_ALARM_SET_HIGH] = write_val;
                break;

            case REG_ALARM_SET_LOW:
                // 调用抽象闹钟接口处理
                if (alarm_handle_modbus_write(REG_ALARM_SET_LOW, write_val) != ALARM_OK) { return false; }
                g_registers[REG_ALARM_SET_LOW] = write_val;
                break;

            // 情况3：删除闹钟（值=闹钟ID）
            case REG_DELETE_ALARM:
                if (alarm_handle_modbus_write(REG_DELETE_ALARM, write_val) != ALARM_OK) { return false; }
                g_registers[REG_DELETE_ALARM] = write_val;
                break;

            // 情况4：执行快捷键（值=1/2，触发对应动作）
            case REG_EXECUTE_SHORTCUT:
                if (write_val == 1)
                {
                    // 执行快捷键1：使用预设的快捷键1配置
                    // g_registers[REG_HEATING_STATUS] = 1;
                    // g_registers[REG_HEATING_LEVEL] = g_registers[REG_SHORTCUT_KEY1];
                }
                else if (write_val == 2)
                {
                    // 执行快捷键2：使用预设的快捷键2配置
                    // g_registers[REG_HEATING_STATUS] = 1;
                    // g_registers[REG_HEATING_LEVEL] = g_registers[REG_SHORTCUT_KEY2];
                }
                else
                {
                    return false; // 非法快捷键ID
                }
                break;

            // 情况5：其他特殊寄存器（预留）
            default:
                return false; // 未知特殊寄存器，写入失败
        }
    }
    return true;
}

// 4. Modbus响应发送（统一封装CRC计算与蓝牙发送）
static void _send_modbus_response(uint8_t *response_buf, uint16_t response_len)
{
    // 计算CRC16（Modbus协议：低字节在前，高字节在后）
    uint16_t crc = Modbus_CRC16(response_buf, response_len);
    response_buf[response_len] = crc & 0xFF;            // CRC低字节
    response_buf[response_len + 1] = (crc >> 8) & 0xFF; // CRC高字节

    // 蓝牙发送（若bt401_sendbytes未实现分包，需补充分包逻辑）
    bt401_sendbytes(response_buf, response_len + 2);
}

// -------------------------- 核心：Modbus消息处理任务 --------------------------
void vModbusProcessTask(void *pvParameters)
{
    (void) pvParameters;

    uint8_t  modbus_rx_frame[MODBUS_FRAME_MAX_LEN] = {0}; // 接收帧缓冲区
    uint8_t  modbus_tx_frame[MODBUS_FRAME_MAX_LEN] = {0}; // 响应帧缓冲区
    uint16_t tx_len = 0;                                  // 响应帧长度（不含CRC）

    for (;;)
    {
        // 从队列阻塞接收Modbus帧（无数据时挂起任务，不占用CPU）
        if (xQueueReceive(xQueue_Modbus, modbus_rx_frame, portMAX_DELAY) != pdTRUE) continue;

        // 解析帧头核心字段
        uint8_t        slave_addr = modbus_rx_frame[0];                           // 从站地址
        uint8_t        func_code = modbus_rx_frame[1];                            // 功能码
        uint16_t       reg_addr = (modbus_rx_frame[2] << 8) | modbus_rx_frame[3]; // 起始寄存器地址
        uint16_t       reg_count = 0;                                             // 寄存器数量（读/写多寄存器共用）
        const uint8_t *write_data = &modbus_rx_frame[7];                          // 写多寄存器的数据起始地址

        // 初始化响应帧基础信息（从站地址+功能码）
        modbus_tx_frame[0] = slave_addr;
        modbus_tx_frame[1] = func_code;
        tx_len = 2;

        // -------------------------- 1. 通用合法性前置检查 --------------------------
        // 检查起始寄存器地址是否超出范围
        if (reg_addr >= REG_COUNT)
        {
            modbus_tx_frame[1] |= 0x80; // 置位异常标志
            modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_ADDR;
            tx_len = 3;
            _send_modbus_response(modbus_tx_frame, tx_len);
            memset(modbus_rx_frame, 0, MODBUS_FRAME_MAX_LEN);
            continue;
        }

        // -------------------------- 2. 按功能码处理 --------------------------
        switch (func_code)
        {
            // -------------------------- 功能码0x03：读保持寄存器 --------------------------
            case MODBUS_FUNC_READ_HOLDING_REG:
                // 解析请求的寄存器数量（帧第4-5字节）
                reg_count = (modbus_rx_frame[4] << 8) | modbus_rx_frame[5];

                // 检查读范围合法性：是否超出寄存器总数
                if (reg_addr + reg_count > REG_COUNT)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_ADDR;
                    tx_len = 3;
                    break;
                }

                // 检查读权限：是否包含只写寄存器（REG_EXECUTE_SHORTCUT及之前）
                if (reg_addr <= REG_EXECUTE_SHORTCUT)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                // 准备响应数据：第2字节=数据总字节数（寄存器数*2）
                modbus_tx_frame[2] = reg_count * 2;
                tx_len = 3;

                // 填充寄存器数据（大端序：高字节在前）
                for (uint16_t i = 0; i < reg_count; i++)
                {
                    uint16_t reg_val = _register_get_value((RegisterID) (reg_addr + i));
                    if (reg_val == 0xFFFF) // 非法值（如越权读）
                    {
                        modbus_tx_frame[1] |= 0x80;
                        modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                        tx_len = 3;
                        break;
                    }
                    modbus_tx_frame[tx_len++] = (reg_val >> 8) & 0xFF; // 高字节
                    modbus_tx_frame[tx_len++] = reg_val & 0xFF;        // 低字节
                }
                break;

            // -------------------------- 功能码0x10：写多个寄存器 --------------------------
            case MODBUS_FUNC_WRITE_MULTIPLE_REG:
                // 解析请求的寄存器数量（帧第4-5字节）和数据字节数（帧第6字节）
                reg_count = (modbus_rx_frame[4] << 8) | modbus_rx_frame[5];
                uint8_t byte_count = modbus_rx_frame[6];

                // 合法性检查1：数据字节数必须=寄存器数*2（Modbus协议要求）
                if (byte_count != reg_count * 2)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                // 合法性检查2：写入范围是否超出寄存器总数
                if (reg_addr + reg_count > REG_COUNT)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_ADDR;
                    tx_len = 3;
                    break;
                }

                // 分两类处理：特殊只写寄存器 + 普通读写寄存器
                bool     write_success = true;
                uint16_t special_reg_start = 0; // 特殊寄存器起始索引
                uint16_t special_reg_count = 0; // 特殊寄存器数量

                // 步骤1：识别特殊寄存器范围（REG_EXECUTE_SHORTCUT及之前）
                if (reg_addr <= REG_EXECUTE_SHORTCUT)
                {
                    // 计算本次写入中特殊寄存器的范围
                    special_reg_start = reg_addr;
                    special_reg_count = (reg_addr + reg_count > REG_EXECUTE_SHORTCUT + 1)
                                            ? (REG_EXECUTE_SHORTCUT + 1 - reg_addr)
                                            : reg_count;

                    // 处理特殊寄存器
                    if (!_process_special_regs(special_reg_start, special_reg_count, write_data))
                    {
                        write_success = false;
                        break;
                    }
                }

                // 步骤2：处理普通读写寄存器（REG_HEATING_STATUS及之后）
                if (write_success && (reg_addr + reg_count > REG_EXECUTE_SHORTCUT + 1))
                {
                    // 计算普通寄存器的起始索引和数量
                    uint16_t normal_reg_start =
                        (reg_addr > REG_EXECUTE_SHORTCUT) ? reg_addr : (REG_EXECUTE_SHORTCUT + 1);
                    uint16_t       normal_reg_count = reg_count - special_reg_count;
                    const uint8_t *normal_write_data = write_data + special_reg_count * 2;

                    // 批量写入普通寄存器
                    for (uint16_t i = 0; i < normal_reg_count; i++)
                    {
                        RegisterID curr_reg = (RegisterID) (normal_reg_start + i);
                        uint16_t   write_val = (normal_write_data[i * 2] << 8) | normal_write_data[i * 2 + 1];
                        if (!_register_set_normal(curr_reg, write_val))
                        {
                            write_success = false;
                            break;
                        }
                        // do reg change actions here if needed
                        do_reg_change_actions(curr_reg, write_val);
                    }
                }

                // 步骤3：处理写入结果
                if (!write_success)
                {
                    modbus_tx_frame[1] |= 0x80;
                    modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_VAL;
                    tx_len = 3;
                    break;
                }

                // 写入成功：响应"起始地址+寄存器数量"（Modbus协议要求）
                modbus_tx_frame[2] = (reg_addr >> 8) & 0xFF;  // 起始地址高字节
                modbus_tx_frame[3] = reg_addr & 0xFF;         // 起始地址低字节
                modbus_tx_frame[4] = (reg_count >> 8) & 0xFF; // 寄存器数量高字节
                modbus_tx_frame[5] = reg_count & 0xFF;        // 寄存器数量低字节
                tx_len = 6;
                break;

            // -------------------------- 非法功能码（仅支持0x03和0x10） --------------------------
            default:
                modbus_tx_frame[1] |= 0x80;
                modbus_tx_frame[2] = MODBUS_EXCEPTION_ILLEGAL_FUNC;
                tx_len = 3;
                break;
        }

        // -------------------------- 3. 发送响应并清空缓冲区 --------------------------
        _send_modbus_response(modbus_tx_frame, tx_len);
        memset(modbus_rx_frame, 0, MODBUS_FRAME_MAX_LEN); // 清空接收帧，避免残留
        memset(modbus_tx_frame, 0, MODBUS_FRAME_MAX_LEN); // 清空响应帧，准备下一次
    }
}
