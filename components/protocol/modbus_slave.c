/**
 * @file modbus_slave.c
 * @brief Modbus协议优化实现
 * @version 2.0
 */
#include "modbus_slave.h"
#include "FreeRTOS.h"
#include "bsp_rtc.h"
#include "app_handles.h"
#include "crc16.h"
#include "semphr.h"
#include "task.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* 宏定义优化 */
#define MODBUS_SLAVE_ADDR 0x01
#define MODBUS_FUNC_READ_HOLDING 0x03
#define MODBUS_FUNC_WRITE_MULTIPLE 0x10
#define MODBUS_FUNC_WRITE_SINGLE 0x06
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_VALUE 0x03

#define MIN_FRAME_LEN 4
#define READ_FRAME_MIN_LEN 8
#define WRITE_MULTI_MIN_LEN 9
#define MAX_REGISTERS_PER_FRAME 125  // Modbus标准限制
#define RESPONSE_BUF_SIZE 256
#define HEATING_REG_COUNT 3  // 加热状态相关寄存器数量

/* 寄存器描述表 */
const RegisterDescriptor g_register_table[REG_COUNT] = {
    {"PowerSwitch", REG_ACCESS_WRITE_ONLY, 0, 1, 0},       {"UTCTimeHigh", REG_ACCESS_WRITE_ONLY, 0, 0xFFFF, 0},
    {"UTCTimeLow", REG_ACCESS_WRITE_ONLY, 0, 0xFFFF, 0},   {"AlarmSetHigh", REG_ACCESS_WRITE_ONLY, 0, 0xFFFF, 0},
    {"AlarmSetLow", REG_ACCESS_WRITE_ONLY, 0, 0xFFFF, 0},  {"DeleteAlarm", REG_ACCESS_WRITE_ONLY, 0, 2, 0},
    {"ExecuteShortcut", REG_ACCESS_WRITE_ONLY, 0, 2, 0},   {"HeatingStatus", REG_ACCESS_READ_WRITE, 0, 1, 0},
    {"HeatingLevel", REG_ACCESS_READ_WRITE, 0, 2, 0},      {"HeatingTimer", REG_ACCESS_READ_WRITE, 0, 720, 0},
    {"ShortcutKey1", REG_ACCESS_READ_WRITE, 0, 0xFFFF, 0}, {"ShortcutKey2", REG_ACCESS_READ_WRITE, 0, 0xFFFF, 0},
};

/* 静态变量 */
static uint16_t g_registers[REG_COUNT]        = {0};
static RegisterWriteCallback g_write_callback = NULL;
static RegisterReadCallback g_read_callback   = NULL;
static SemaphoreHandle_t g_reg_mutex          = NULL;  // 寄存器互斥锁

/* 内部函数声明 */
static bool is_valid_register(RegisterID reg);
static bool check_register_access(RegisterID reg, bool is_write);
static bool validate_register_value(RegisterID reg, uint16_t value);
static ProtocolResult handle_read_registers(uint8_t slave_addr, uint16_t start, uint16_t count, uint8_t* response,
                                            uint16_t* resp_len);
static ProtocolResult handle_write_registers(uint8_t slave_addr, uint16_t start, uint16_t count, const uint8_t* data,
                                             uint16_t data_len, uint8_t* response, uint16_t* resp_len);
static ProtocolResult handle_write_single_register(uint8_t slave_addr, uint16_t reg, uint16_t value, uint8_t* response,
                                                   uint16_t* resp_len);
static void build_exception_response(uint8_t slave_addr, uint8_t func_code, uint8_t exception_code, uint8_t* response,
                                     uint16_t* resp_len);
static void build_success_response(uint8_t slave_addr, uint8_t func_code, const uint8_t* data, uint16_t data_len,
                                   uint8_t* response, uint16_t* resp_len);
static bool process_utc_timestamp(uint16_t start_reg, uint16_t reg_num, const uint8_t* write_buf);
static ProtocolResult internal_register_write(RegisterID reg, uint16_t value);

/**
 * @brief 寄存器互斥锁保护宏
 * @note  使用 FreeRTOS 互斥锁替代关中断，避免长时间阻塞系统中断和调度
 *        回调函数在持锁期间执行，调用者需确保回调不会长时间阻塞
 */
#define REG_LOCK() (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(100)))
#define REG_UNLOCK() (xSemaphoreGive(g_reg_mutex))

/**
 * @brief 初始化协议模块（创建互斥锁，需在使用前调用一次）
 */
void protocol_init(void) {
    if (g_reg_mutex == NULL) {
        g_reg_mutex = xSemaphoreCreateMutex();
        configASSERT(g_reg_mutex != NULL);
    }
}

/**
 * @brief 检查寄存器ID是否有效
 */
static bool is_valid_register(RegisterID reg) {
    return (reg < REG_COUNT);
}

/**
 * @brief 
 * 
 */
static bool check_register_access(RegisterID reg, bool is_write) {
    if (!is_valid_register(reg)) { return false; }

    RegisterAccess access = g_register_table[reg].access;

    if (is_write) {
        return (access == REG_ACCESS_WRITE_ONLY || access == REG_ACCESS_READ_WRITE);
    } else {
        return (access == REG_ACCESS_READ_ONLY || access == REG_ACCESS_READ_WRITE);
    }
}

/**
 * @brief 验证寄存器值范围
 */
static bool validate_register_value(RegisterID reg, uint16_t value) {
    if (!is_valid_register(reg)) { return false; }

    const RegisterDescriptor* desc = &g_register_table[reg];

    // 特殊处理：某些寄存器有特殊验证规则
    switch (reg) {
    case REG_DELETE_ALARM:
    case REG_EXECUTE_SHORTCUT: return (value == 1 || value == 2);
    case REG_UTC_TIMESTAMP_HIGH:
    case REG_UTC_TIMESTAMP_LOW:
    case REG_ALARM_SET_HIGH:
    case REG_ALARM_SET_LOW:
        // 这些寄存器由组合逻辑验证
        return true;
    default:
        // 通用范围验证
        if (desc->min_val == 0 && desc->max_val == 0) {
            return true;  // 无范围限制
        }
        return (value >= desc->min_val && value <= desc->max_val);
    }
}

/**
 * @brief 处理UTC时间戳特殊逻辑
 */
static bool process_utc_timestamp(uint16_t start_reg, uint16_t reg_num, const uint8_t* write_buf) {
    // 检查是否有足够的数据处理UTC时间戳
    if ((start_reg == REG_UTC_TIMESTAMP_HIGH) && (reg_num >= 2)) {
        uint16_t high_val = (write_buf[0] << 8) | write_buf[1];
        uint16_t low_val  = (write_buf[2] << 8) | write_buf[3];
        uint32_t utc_full = ((uint32_t)high_val << 16) | low_val;

        // 更新寄存器
        g_registers[REG_UTC_TIMESTAMP_HIGH] = high_val;
        g_registers[REG_UTC_TIMESTAMP_LOW]  = low_val;

        // 设置RTC
        if (bsp_rtc_set_utc(g_rtc, utc_full) == BSP_OK) { return true; }
    }
    return false;
}

/**
 * @brief 写入单个寄存器（内部实现）
 */
static ProtocolResult internal_register_write(RegisterID reg, uint16_t value) {
    if (!is_valid_register(reg)) { return PROTOCOL_ERR_INVALID_REG; }

    if (!check_register_access(reg, true)) { return PROTOCOL_ERR_WRITE_ONLY; }

    if (!validate_register_value(reg, value)) { return PROTOCOL_ERR_INVALID_VALUE; }

    if (REG_LOCK() != pdTRUE) { return PROTOCOL_ERR_BUSY; }

    // 特殊寄存器处理
    switch (reg) {
    case REG_UTC_TIMESTAMP_HIGH:
    case REG_UTC_TIMESTAMP_LOW:
        // UTC时间戳需要高位和低位一起处理，不能单独写入
        REG_UNLOCK();
        return PROTOCOL_ERR_INVALID_VALUE;

    default:
        g_registers[reg] = value;

        // 调用写回调通知应用层
        if (g_write_callback != NULL) { g_write_callback(reg, value); }
        break;
    }

    REG_UNLOCK();
    return PROTOCOL_SUCCESS;
}

/**
 * @brief 处理读取寄存器请求
 */
static ProtocolResult handle_read_registers(uint8_t slave_addr, uint16_t start, uint16_t count, uint8_t* response,
                                            uint16_t* resp_len) {
    if (count == 0 || count > MAX_REGISTERS_PER_FRAME) { return PROTOCOL_ERR_INVALID_VALUE; }

    if ((start + count) > REG_COUNT) { return PROTOCOL_ERR_INVALID_REG; }

    // 检查所有寄存器是否都可读
    for (uint16_t i = 0; i < count; i++) {
        RegisterID reg = start + i;
        if (!check_register_access(reg, false)) { return PROTOCOL_ERR_READ_ONLY; }
    }

    uint8_t data_buf[RESPONSE_BUF_SIZE];
    uint16_t data_len = 0;

    data_buf[data_len++] = (uint8_t)(count * 2);  // 字节数

    if (REG_LOCK() != pdTRUE) { return PROTOCOL_ERR_BUSY; }

    for (uint16_t i = 0; i < count; i++) {
        RegisterID reg = start + i;
        uint16_t value;

        // 优先使用回调函数读取
        if (g_read_callback != NULL) {
            value = g_read_callback(reg);
        } else {
            value = g_registers[reg];
        }

        data_buf[data_len++] = (uint8_t)((value >> 8) & 0xFF);
        data_buf[data_len++] = (uint8_t)(value & 0xFF);
    }

    REG_UNLOCK();

    build_success_response(slave_addr, MODBUS_FUNC_READ_HOLDING, data_buf, data_len, response, resp_len);
    return PROTOCOL_SUCCESS;
}

/**
 * @brief 处理写入多个寄存器请求
 */
static ProtocolResult handle_write_registers(uint8_t slave_addr, uint16_t start, uint16_t count, const uint8_t* data,
                                             uint16_t data_len, uint8_t* response, uint16_t* resp_len) {
    if (count == 0 || count > MAX_REGISTERS_PER_FRAME) { return PROTOCOL_ERR_INVALID_VALUE; }

    if ((start + count) > REG_COUNT) { return PROTOCOL_ERR_INVALID_REG; }

    if (data_len != (count * 2)) { return PROTOCOL_ERR_INVALID_FRAME; }

    // ---- 阶段1: 预验证所有寄存器（临界区外） ----
    uint16_t bytes_processed = 0;
    for (uint16_t i = 0; i < count; i++) {
        RegisterID reg = start + i;

        // UTC时间戳寄存器跳过单独验证（由组合逻辑处理）
        if (reg == REG_UTC_TIMESTAMP_HIGH || reg == REG_UTC_TIMESTAMP_LOW) {
            bytes_processed += 2;
            continue;
        }

        uint16_t value = (data[bytes_processed] << 8) | data[bytes_processed + 1];
        bytes_processed += 2;

        if (!check_register_access(reg, true)) { return PROTOCOL_ERR_WRITE_ONLY; }

        if (!validate_register_value(reg, value)) { return PROTOCOL_ERR_INVALID_VALUE; }
    }

    // ---- 阶段2: 原子写入所有寄存器（互斥锁保护） ----
    if (REG_LOCK() != pdTRUE) { return PROTOCOL_ERR_BUSY; }

    // 处理特殊寄存器组合（如UTC时间戳）
    if (start <= REG_UTC_TIMESTAMP_LOW && (start + count) > REG_UTC_TIMESTAMP_HIGH) {
        if (!process_utc_timestamp(start, count, data)) {
            REG_UNLOCK();
            return PROTOCOL_ERR_INVALID_VALUE;
        }
    }

    bytes_processed = 0;
    for (uint16_t i = 0; i < count; i++) {
        RegisterID reg = start + i;

        // 跳过已由 process_utc_timestamp 处理的UTC寄存器
        if (reg == REG_UTC_TIMESTAMP_HIGH || reg == REG_UTC_TIMESTAMP_LOW) {
            bytes_processed += 2;
            continue;
        }

        uint16_t value = (data[bytes_processed] << 8) | data[bytes_processed + 1];
        bytes_processed += 2;

        g_registers[reg] = value;
    }

    REG_UNLOCK();

    // ---- 阶段3: 通知应用层（临界区外，避免长时间关调度） ----
    bytes_processed = 0;
    for (uint16_t i = 0; i < count; i++) {
        RegisterID reg = start + i;
        if (reg == REG_UTC_TIMESTAMP_HIGH || reg == REG_UTC_TIMESTAMP_LOW) {
            bytes_processed += 2;
            continue;
        }

        uint16_t value = (data[bytes_processed] << 8) | data[bytes_processed + 1];
        bytes_processed += 2;

        if (g_write_callback != NULL) { g_write_callback(reg, value); }
    }

    // 构建成功响应
    build_success_response(slave_addr, MODBUS_FUNC_WRITE_MULTIPLE, NULL, 0, response, resp_len);
    return PROTOCOL_SUCCESS;
}

/**
 * @brief 处理写单个寄存器请求
 */
static ProtocolResult handle_write_single_register(uint8_t slave_addr, uint16_t reg, uint16_t value, uint8_t* response,
                                                   uint16_t* resp_len) {
    if (reg >= REG_COUNT) { return PROTOCOL_ERR_INVALID_REG; }

    ProtocolResult result = internal_register_write((RegisterID)reg, value);
    if (result != PROTOCOL_SUCCESS) { return result; }

    // 构建成功响应（返回写入的值）
    uint8_t data[4] = {(uint8_t)((reg >> 8) & 0xFF), (uint8_t)(reg & 0xFF), (uint8_t)((value >> 8) & 0xFF),
                       (uint8_t)(value & 0xFF)};

    build_success_response(slave_addr, MODBUS_FUNC_WRITE_SINGLE, data, 4, response, resp_len);
    return PROTOCOL_SUCCESS;
}

/**
 * @brief 构建异常响应
 */
static void build_exception_response(uint8_t slave_addr, uint8_t func_code, uint8_t exception_code, uint8_t* response,
                                     uint16_t* resp_len) {
    response[0] = slave_addr;
    response[1] = func_code | 0x80;  // 设置异常标志
    response[2] = exception_code;
    *resp_len   = 3;
}

/**
 * @brief 构建成功响应
 */
static void build_success_response(uint8_t slave_addr, uint8_t func_code, const uint8_t* data, uint16_t data_len,
                                   uint8_t* response, uint16_t* resp_len) {
    response[0] = slave_addr;
    response[1] = func_code;

    if (data != NULL && data_len > 0) {
        memcpy(&response[2], data, data_len);
        *resp_len = data_len + 2;
    } else {
        *resp_len = 2;
    }
}

/**
 * @brief 计算CRC16校验码
 */
uint16_t protocol_calc_crc16(const uint8_t* data, uint16_t len) {
    return Modbus_CRC16(data, len);
}

/**
 * @brief 验证CRC
 */
bool protocol_validate_crc(const uint8_t* data, uint16_t len) {
    if (len < 2) { return false; }

    uint16_t crc_calc = protocol_calc_crc16(data, len - 2);
    uint16_t crc_recv = (data[len - 1] << 8) | data[len - 2];  // 注意：Modbus CRC是小端序

    return (crc_calc == crc_recv);
}

/**
 * @brief 处理Modbus协议帧
 */
ProtocolResult protocol_process_frame(const uint8_t* data, uint16_t len, uint8_t* response, uint16_t* resp_len) {
    if (data == NULL || response == NULL || resp_len == NULL) { return PROTOCOL_ERR_INVALID_FRAME; }

    if (len < MIN_FRAME_LEN) { return PROTOCOL_ERR_INVALID_FRAME; }

    // 验证从机地址
    uint8_t slave_addr = data[0];
    if (slave_addr != MODBUS_SLAVE_ADDR) { return PROTOCOL_ERR_INVALID_ADDR; }

    // 验证CRC
    if (!protocol_validate_crc(data, len)) { return PROTOCOL_ERR_CRC; }

    uint8_t func_code     = data[1];
    ProtocolResult result = PROTOCOL_SUCCESS;

    switch (func_code) {
    case MODBUS_FUNC_READ_HOLDING: {
        if (len < READ_FRAME_MIN_LEN) {
            result = PROTOCOL_ERR_INVALID_FRAME;
            break;
        }

        uint16_t start_reg = (data[2] << 8) | data[3];
        uint16_t reg_count = (data[4] << 8) | data[5];

        result = handle_read_registers(slave_addr, start_reg, reg_count, response, resp_len);
        break;
    }

    case MODBUS_FUNC_WRITE_MULTIPLE: {
        if (len < WRITE_MULTI_MIN_LEN) {
            result = PROTOCOL_ERR_INVALID_FRAME;
            break;
        }

        uint16_t start_reg = (data[2] << 8) | data[3];
        uint16_t reg_count = (data[4] << 8) | data[5];
        uint8_t byte_count = data[6];

        if ((7 + byte_count) > len) {
            result = PROTOCOL_ERR_INVALID_FRAME;
            break;
        }

        const uint8_t* write_data = &data[7];

        result = handle_write_registers(slave_addr, start_reg, reg_count, write_data, byte_count, response, resp_len);
        break;
    }

    case MODBUS_FUNC_WRITE_SINGLE: {
        if (len != 8) {  // 地址+功能码+寄存器地址+寄存器值+CRC(2字节)
            result = PROTOCOL_ERR_INVALID_FRAME;
            break;
        }

        uint16_t reg_addr  = (data[2] << 8) | data[3];
        uint16_t reg_value = (data[4] << 8) | data[5];

        result = handle_write_single_register(slave_addr, reg_addr, reg_value, response, resp_len);
        break;
    }

    default: result = PROTOCOL_ERR_INVALID_FUNC; break;
    }

    // 如果是错误，构建异常响应
    if (result != PROTOCOL_SUCCESS) {
        uint8_t exception_code;

        switch (result) {
        case PROTOCOL_ERR_INVALID_ADDR:
        case PROTOCOL_ERR_INVALID_REG:
        case PROTOCOL_ERR_WRITE_ONLY:  // 读取了只写寄存器
        case PROTOCOL_ERR_READ_ONLY:   // 写入了只读寄存器
            exception_code = MODBUS_EXCEPTION_ILLEGAL_ADDRESS;
            break;
        case PROTOCOL_ERR_INVALID_VALUE: exception_code = MODBUS_EXCEPTION_ILLEGAL_VALUE; break;
        case PROTOCOL_ERR_INVALID_FUNC:
        default: exception_code = MODBUS_EXCEPTION_ILLEGAL_FUNCTION; break;
        }

        build_exception_response(slave_addr, func_code, exception_code, response, resp_len);
    }

    return result;
}

/**
 * @brief 处理Modbus请求帧并发送响应（包含异常响应）
 */
bool protocol_handle_request(const uint8_t* data, size_t len) {
    uint8_t response[RESPONSE_BUF_SIZE];
    uint16_t resp_len = 0;

    ProtocolResult result = protocol_process_frame(data, (uint16_t)len, response, &resp_len);

    /* 有响应数据（成功或异常）才发送 */
    if (resp_len > 0) {
        uint16_t crc           = protocol_calc_crc16(response, resp_len);
        response[resp_len]     = (uint8_t)(crc & 0xFF);
        response[resp_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

        bsp_bt401_send(g_bt401, response, resp_len + 2);
        return (result == PROTOCOL_SUCCESS);
    }

    return false;
}

/**
 * @brief 写入单个寄存器
 */
ProtocolResult register_write(RegisterID reg, uint16_t value) {
    return internal_register_write(reg, value);
}

/**
 * @brief 批量写入寄存器
 */
ProtocolResult register_batch_write(RegisterID start_reg, const uint16_t* values, uint8_t count) {
    if (!is_valid_register(start_reg) || ((uint16_t)start_reg + count) > REG_COUNT || values == NULL) {
        return PROTOCOL_ERR_INVALID_REG;
    }

    // 预验证
    for (uint8_t i = 0; i < count; i++) {
        RegisterID reg = (RegisterID)(start_reg + i);

        if (!check_register_access(reg, true)) { return PROTOCOL_ERR_WRITE_ONLY; }

        if (!validate_register_value(reg, values[i])) { return PROTOCOL_ERR_INVALID_VALUE; }
    }

    if (REG_LOCK() != pdTRUE) { return PROTOCOL_ERR_BUSY; }

    for (uint8_t i = 0; i < count; i++) {
        RegisterID reg   = (RegisterID)(start_reg + i);
        g_registers[reg] = values[i];

        if (g_write_callback != NULL) { g_write_callback(reg, values[i]); }
    }

    REG_UNLOCK();
    return PROTOCOL_SUCCESS;
}

/**
 * @brief 读取单个寄存器
 */
uint16_t register_read(RegisterID reg) {
    if (!is_valid_register(reg)) { return 0xFFFF; }

    uint16_t value;

    if (REG_LOCK() != pdTRUE) { return 0xFFFF; }

    if (g_read_callback != NULL) {
        value = g_read_callback(reg);
    } else {
        value = g_registers[reg];
    }

    REG_UNLOCK();
    return value;
}

/**
 * @brief 批量读取寄存器
 */
ProtocolResult register_batch_read(RegisterID start_reg, uint16_t* values, uint8_t count) {
    if (!is_valid_register(start_reg) || ((uint16_t)start_reg + count) > REG_COUNT || values == NULL) {
        return PROTOCOL_ERR_INVALID_REG;
    }

    if (REG_LOCK() != pdTRUE) { return PROTOCOL_ERR_BUSY; }

    for (uint8_t i = 0; i < count; i++) {
        RegisterID reg = (RegisterID)(start_reg + i);

        if (!check_register_access(reg, false)) {
            REG_UNLOCK();
            return PROTOCOL_ERR_READ_ONLY;
        }

        if (g_read_callback != NULL) {
            values[i] = g_read_callback(reg);
        } else {
            values[i] = g_registers[reg];
        }
    }

    REG_UNLOCK();
    return PROTOCOL_SUCCESS;
}

/**
 * @brief 注册写回调函数
 */
void protocol_register_write_callback(RegisterWriteCallback cb) {
    g_write_callback = cb;
}

/**
 * @brief 注册读回调函数
 */
void protocol_register_read_callback(RegisterReadCallback cb) {
    g_read_callback = cb;
}

/**
 * @brief 上传加热状态
 */
void protocol_upload_heating_status(void) {
    uint8_t response[RESPONSE_BUF_SIZE] = {0};

    // 构建响应头：地址 + 功能码 + 字节数
    response[0] = MODBUS_SLAVE_ADDR;
    response[1] = MODBUS_FUNC_READ_HOLDING;
    response[2] = HEATING_REG_COUNT * 2;  // N个寄存器 × 2字节

    if (REG_LOCK() != pdTRUE) { return; }

    for (uint8_t i = 0; i < HEATING_REG_COUNT; i++) {
        RegisterID reg = REG_HEATING_STATUS + i;
        uint16_t value;

        if (g_read_callback != NULL) {
            value = g_read_callback(reg);
        } else {
            value = g_registers[reg];
        }

        response[3 + (i * 2)]     = (uint8_t)((value >> 8) & 0xFF);
        response[3 + (i * 2) + 1] = (uint8_t)(value & 0xFF);
    }

    REG_UNLOCK();

    uint16_t resp_len = 3 + (HEATING_REG_COUNT * 2);

    // 计算CRC并发送
    uint16_t crc           = protocol_calc_crc16(response, resp_len);
    response[resp_len]     = (uint8_t)(crc & 0xFF);
    response[resp_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    bsp_bt401_send(g_bt401, response, resp_len + 2);
}
