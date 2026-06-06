/**
 * @file ble_service.h
 * @brief BLE服务接口：帧分发 + AT命令
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef __cplusplus
#ifndef bool
#define bool _Bool
#define true 1
#define false 0
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { VOL_DIR_UP, VOL_DIR_DOWN } volume_dir_t;
typedef enum { BT_MODE_OFF, BT_MODE_BT, BT_MODE_MUSIC } bt_mode_t;

/* 帧处理回调：上层注册以解耦协议层 */
typedef void (*ble_frame_cb_t)(const uint8_t* data, uint16_t len, bool is_modbus);

void ble_service_init(void);
void ble_service_set_frame_handler(ble_frame_cb_t cb);
void bt_start(void);
void ble_mode(bt_mode_t mode);
void ble_query(void);
void music_switch(void);
void music_next(void);
void music_prev(void);
void music_volume_control(volume_dir_t dir);
void music_volume_set(uint8_t vol);

#ifdef __cplusplus
}
#endif
#endif
