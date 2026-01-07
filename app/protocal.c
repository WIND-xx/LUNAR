
/**
 * @file protocal.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief Modbus协议实现，用于寄存器管理和通信
 * @version 0.1
 * @date 2025-11-30
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "protocal.h"
#include "BufferProcess.h"
#include "FreeRTOS.h"
#include "bsp_rtc.h"
#include "bt401.h"
#include "crc16.h"
#include "task.h"
#include <stdbool.h>
#include <stdint.h>

/* Modbus功能码定义 */
#define MODBUS_FUNC_READ_HOLDING   0x03  // 读保持寄存器
#define MODBUS_FUNC_WRITE_MULTIPLE 0x10  // 写多个寄存器

/* Modbus通信参数 */
#define MODBUS_SLAVE_ADDR          0x01  // 从机设备地址
#define MODBUS_MIN_FRAME_LEN       4     // 最小帧长度（地址+功能码+CRC）
#define MODBUS_READ_FRAME_MIN_LEN  8     // 读操作最小帧长度
#define MODBUS_WRITE_FRAME_MIN_LEN 9     // 写操作最小帧长度

/* 外部函数，用于处理寄存器变化后的动作 */
extern void     do_reg_change_actions(RegisterID reg, uint16_t value);

/* 全局寄存器存储数组 */
static uint16_t g_registers[REG_COUNT] = {0};

/* 内部函数声明 */
static uint16_t _register_get_value(RegisterID reg_id);
static bool     _register_set_normal(RegisterID reg_id, uint16_t value);
static bool     _process_special_regs(uint16_t start_reg, uint16_t reg_num, const uint8_t *write_buf);
static void     _send_modbus_response(uint8_t *response_buf, uint16_t response_len);
static bool     validate_register_range(uint16_t start_reg, uint16_t reg_num);

/**
 * @brief 线程安全地获取寄存器值
 * @param reg_id 要读取的寄存器ID
 * @return 寄存器值，如果寄存器ID无效则返回0xFFFF
 */
static uint16_t _register_get_value(RegisterID reg_id)
{
    uint16_t value = 0xFFFF;
    
    // 进入临界区保证线程安全读取
    taskENTER_CRITICAL();
    if (reg_id < REG_COUNT) 
    {
        value = g_registers[reg_id];
    }
    taskEXIT_CRITICAL();
    
    return value;
}

/**
 * @brief 设置普通寄存器值并进行合法性验证
 * @param reg_id 要写入的寄存器ID
 * @param value 要写入的值
 * @return true 如果值合法且写入成功，否则返回false
 */
static bool _register_set_normal(RegisterID reg_id, uint16_t value)
{
    bool ret = false;
    
    taskENTER_CRITICAL();
    
    // 验证寄存器ID有效性
    if (reg_id >= REG_COUNT)
    {
        taskEXIT_CRITICAL();
        return ret;
    }

    // 根据寄存器类型验证值的合法性
    switch (reg_id)
    {
        case REG_HEATING_LEVEL:
            ret = (value <= 2);  // 有效加热档位：0, 1, 2
            break;
        case REG_HEATING_TIMER:
            ret = (value <= 720);  // 最大定时值：720分钟（12小时）
            break;
        case REG_HEATING_STATUS:
            ret = (value <= 1);  // 有效状态：0（关闭），1（开启）
            break;
        default:
            ret = true;  // 其他寄存器无验证要求
    }

    // 如果值合法则写入
    if (ret) 
    {
        g_registers[reg_id] = value;
    }
    
    taskEXIT_CRITICAL();
    return ret;
}

/**
 * @brief 验证寄存器范围是否在有效边界内
 * @param start_reg 起始寄存器地址
 * @param reg_num 寄存器数量
 * @return true 如果范围有效，否则返回false
 */
static bool validate_register_range(uint16_t start_reg, uint16_t reg_num)
{
    return (start_reg < REG_COUNT) && 
           ((start_reg + reg_num) <= REG_COUNT) && 
           (reg_num > 0);
}

/**
 * @brief 处理具有特殊逻辑的寄存器
 * @param start_reg 起始寄存器地址
 * @param reg_num 要处理的寄存器数量
 * @param write_buf 包含待写入寄存器值的缓冲区
 * @return true 如果所有寄存器处理成功，否则返回false
 */
static bool _process_special_regs(uint16_t start_reg, uint16_t reg_num, const uint8_t *write_buf)
{
    for (uint16_t i = 0; i < reg_num; i++)
    {
        RegisterID curr_reg = (RegisterID) (start_reg + i);
        
        // 步骤1：从缓冲区提取16位寄存器值（大端序）
        uint16_t write_val = (write_buf[i * 2] << 8) | write_buf[i * 2 + 1];

        switch (curr_reg)
        {
            case REG_UTC_TIMESTAMP_HIGH:
                // 处理32位UTC时间戳（高位和低位寄存器）
                
                // 确保低位寄存器也在处理范围内
                if ((i + 1) >= reg_num) return false;
                
                // 步骤2：提取UTC时间戳的低16位
                uint16_t utc_low = (write_buf[(i + 1) * 2] << 8) | write_buf[(i + 1) * 2 + 1];
                
                // 步骤3：合并高位和低位形成32位时间戳
                uint32_t utc_full = ((uint32_t) write_val << 16) | utc_low;
                
                // 步骤4：使用合并后的时间戳设置RTC
                if (RTC_SetUTC(utc_full) != 0) return false;
                
                // 步骤5：更新高位和低位寄存器
                g_registers[REG_UTC_TIMESTAMP_HIGH] = write_val;
                g_registers[REG_UTC_TIMESTAMP_LOW] = utc_low;
                
                // 跳过下一次迭代，因为已经处理了两个寄存器
                i++;
                break;

            case REG_UTC_TIMESTAMP_LOW:
                // 低位寄存器只能与高位寄存器一起写入
                return false;
                
            case REG_ALARM_SET_HIGH:
            case REG_ALARM_SET_LOW:
            case REG_DELETE_ALARM:
            case REG_EXECUTE_SHORTCUT:
                // 这些寄存器只接受值1或2
                if ((write_val != 1) && (write_val != 2)) return false;
                g_registers[curr_reg] = write_val;
                break;

            default:
                // 未知的特殊寄存器
                return false;
        }
    }
    return true;
}

/**
 * @brief 发送带CRC校验的Modbus响应
 * @param response_buf 包含响应数据的缓冲区
 * @param response_len 响应数据长度（不包括CRC）
 */
static void _send_modbus_response(uint8_t *response_buf, uint16_t response_len)
{
    // 验证参数合法性
    if (!response_buf || !response_len || (response_len + 2) > MODBUS_FRAME_MAX_LEN) 
        return;
    
    // 步骤1：计算响应的CRC16校验码
    uint16_t crc = Modbus_CRC16(response_buf, response_len);
    
    // 步骤2：将CRC追加到响应末尾（先低字节，后高字节）
    response_buf[response_len] = crc & 0xFF;
    response_buf[response_len + 1] = (crc >> 8) & 0xFF;
    
    // 步骤3：通过蓝牙发送完整的响应
    bt401_sendbytes(response_buf, response_len + 2);
}

/**
 * @brief 解码并处理Modbus协议帧
 * @param data 指向接收帧数据的指针
 * @param len 接收数据的长度
 * @return true 如果帧处理成功，否则返回false
 */
bool decode_protocal(const uint8_t *data, size_t len)
{
    // 步骤1：验证最小帧长度
    if (len < MODBUS_MIN_FRAME_LEN) return false;
    
    // 步骤2：提取从机地址和功能码
    uint8_t slave_addr = data[0];
    uint8_t func_code = data[1];
    
    // 步骤3：验证从机地址是否匹配
    if (slave_addr != MODBUS_SLAVE_ADDR) return false;

    // 准备响应缓冲区
    uint8_t response_buf[MODBUS_FRAME_MAX_LEN] = {0};
    response_buf[0] = slave_addr;
    response_buf[1] = func_code;
    uint16_t response_len = 2;

    switch (func_code)
    {
        case MODBUS_FUNC_READ_HOLDING:
            // ========== 读保持寄存器 ==========
            
            // 步骤4：验证读取帧长度
            if (len < MODBUS_READ_FRAME_MIN_LEN) return false;
            
            // 步骤5：提取起始寄存器地址（大端序）
            uint16_t start_reg = (data[2] << 8) | data[3];
            
            // 步骤6：提取要读取的寄存器数量（大端序）
            uint16_t reg_num = (data[4] << 8) | data[5];
            
            // 步骤7：验证寄存器范围
            if (!validate_register_range(start_reg, reg_num)) return false;
            
            // 步骤8：在响应中设置字节计数（每个寄存器2字节）
            response_buf[2] = (uint8_t) (reg_num * 2);
            response_len = 3;
            
            // 步骤9：读取并打包寄存器值
            for (uint16_t i = 0; i < reg_num; i++)
            {
                RegisterID curr_reg = (RegisterID) (start_reg + i);
                uint16_t   reg_val = _register_get_value(curr_reg);
                
                // 验证寄存器读取成功
                if (reg_val == 0xFFFF) return false;
                
                // 检查缓冲区空间
                if ((response_len + 2) > MODBUS_FRAME_MAX_LEN) return false;
                
                // 打包寄存器值（大端序：先高字节，后低字节）
                response_buf[response_len++] = (reg_val >> 8) & 0xFF;
                response_buf[response_len++] = reg_val & 0xFF;
            }
            
            // 步骤10：发送响应
            _send_modbus_response(response_buf, response_len);
            return true;

        case MODBUS_FUNC_WRITE_MULTIPLE:
            // ========== 写多个寄存器 ==========
            
            // 步骤4：验证写入帧最小长度
            if (len < MODBUS_WRITE_FRAME_MIN_LEN) return false;
            
            // 步骤5：提取起始寄存器地址（大端序）
            start_reg = (data[2] << 8) | data[3];
            
            // 步骤6：提取要写入的寄存器数量（大端序）
            reg_num = (data[4] << 8) | data[5];
            
            // 步骤7：提取字节计数
            uint8_t byte_count = data[6];
            
            // 步骤8：获取寄存器值缓冲区指针
            const uint8_t *write_buf = &data[7];
            
            // 步骤9：验证帧结构
            if ((byte_count != (reg_num * 2)) || 
                !validate_register_range(start_reg, reg_num) ||
                (7U + byte_count > len))
                return false;

            bool     write_success = true;
            uint16_t special_reg_num = 0;

            // 步骤10：如果在范围内，处理特殊寄存器
            if (start_reg <= REG_EXECUTE_SHORTCUT)
            {
                // 计算需要处理的特殊寄存器数量
                special_reg_num =
                    (start_reg + reg_num > REG_EXECUTE_SHORTCUT + 1) ? 
                    (REG_EXECUTE_SHORTCUT + 1 - start_reg) : reg_num;
                
                // 在临界区内处理特殊寄存器
                taskENTER_CRITICAL();
                write_success = _process_special_regs(start_reg, special_reg_num, write_buf);
                taskEXIT_CRITICAL();
                
                if (!write_success) return false;
            }

            // 步骤11：如果还有剩余，处理普通寄存器
            if (write_success && (start_reg + reg_num > REG_EXECUTE_SHORTCUT + 1))
            {
                // 计算普通寄存器范围
                uint16_t normal_reg_start = (start_reg > REG_EXECUTE_SHORTCUT) ? 
                                            start_reg : (REG_EXECUTE_SHORTCUT + 1);
                uint16_t normal_reg_num = reg_num - special_reg_num;
                
                // 调整缓冲区指针以跳过特殊寄存器
                const uint8_t *normal_write_buf = write_buf + (special_reg_num * 2);

                for (uint16_t i = 0; i < normal_reg_num; i++)
                {
                    RegisterID curr_reg = (RegisterID) (normal_reg_start + i);
                    
                    // 提取寄存器值（大端序）
                    uint16_t write_val = (normal_write_buf[i * 2] << 8) | normal_write_buf[i * 2 + 1];
                    
                    // 带验证地写入寄存器
                    if (!_register_set_normal(curr_reg, write_val))
                    {
                        write_success = false;
                        break;
                    }
                    
                    // 触发寄存器变化动作
                    do_reg_change_actions(curr_reg, write_val);
                }
            }

            // 步骤12：如果所有写入成功则发送成功响应
            if (write_success)
            {
                // 打包起始寄存器地址（大端序）
                response_buf[2] = (start_reg >> 8) & 0xFF;
                response_buf[3] = start_reg & 0xFF;
                
                // 打包写入的寄存器数量（大端序）
                response_buf[4] = (reg_num >> 8) & 0xFF;
                response_buf[5] = reg_num & 0xFF;
                
                response_len = 6;
                _send_modbus_response(response_buf, response_len);
                return true;
            }
            return false;

        default:
            // 未知功能码
            return false;
    }
}

/**
 * @brief 向主机上传加热状态寄存器
 * 
 * 此函数主动向主机发送当前的加热状态寄存器
 * （REG_HEATING_STATUS、REG_HEATING_LEVEL、REG_HEATING_TIMER）
 * 采用Modbus读保持寄存器响应格式
 */
void protocal_uplode_heat(void)
{
    // 步骤1：初始化响应缓冲区
    uint8_t response_buf[MODBUS_FRAME_MAX_LEN] = {0};
    
    // 步骤2：设置响应头
    response_buf[0] = MODBUS_SLAVE_ADDR;
    response_buf[1] = MODBUS_FUNC_READ_HOLDING;
    response_buf[2] = 6;  // 3个寄存器 × 2字节 = 6字节
    uint16_t response_len = 3;

    // 步骤3：读取并打包3个加热相关寄存器
    for (uint16_t i = 0; i < 3; i++)
    {
        uint16_t reg_val;
        
        // 步骤4：在临界区内读取寄存器值
        taskENTER_CRITICAL();
        reg_val = g_registers[REG_HEATING_STATUS + i];
        taskEXIT_CRITICAL();
        
        // 步骤5：打包寄存器值（大端序：先高字节，后低字节）
        response_buf[response_len++] = (reg_val >> 8) & 0xFF;
        response_buf[response_len++] = reg_val & 0xFF;
    }

    // 步骤6：发送带CRC的响应
    _send_modbus_response(response_buf, response_len);
}
