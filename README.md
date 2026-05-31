# LUNAR — STM32F103 FreeRTOS 加热控制器

基于 STM32F103C8T6 + FreeRTOS 的智能加热设备固件，支持 BLE 远程控制、矩阵键盘、PID 温控、Modbus RTU 协议。

## 硬件平台

| 组件 | 型号 |
|------|------|
| MCU | STM32F103C8T6 (72MHz, 64KB Flash, 20KB RAM) |
| 蓝牙 | BT401 (UART3, DMA+IDLE) |
| 键盘 | 5x5 矩阵键盘 + 独立电源键 |
| 温度传感器 | NTC 热敏电阻 (12位 ADC) |
| 加热元件 | PWM 驱动 (TIM1_CH4) |
| 显示 | 8 路 LED 指示灯 |
| 时钟 | RTC (LSE 32768Hz) |

## 软件架构

```
app/
├── app_main.c/h                 # 唯一入口，注册回调 + 订阅事件
├── core/                        # 基础设施层
│   ├── app_config.h             #   集中配置（40+ 宏）
│   └── event_bus.c/h            #   事件总线（发布-订阅）
├── service/                     # 业务服务层（零协议依赖）
│   ├── key_service.c/h          #   按键扫描 + 去抖 + 事件发布
│   ├── heat_service.c/h         #   加热状态机 + PID 温控
│   └── ble_service.c/h          #   BLE 帧分发 + AT 命令 + 看门狗
├── protocol/                    # 协议层（零服务依赖）
│   ├── modbus_slave.c/h         #   Modbus RTU 从站
│   ├── at_parser.c/h            #   AT 命令解析器
│   └── at_command_manager.c/h   #   AT 命令队列（静态池）
bsp/                             # 板级驱动
├── bt401.c/h                    #   BLE UART DMA 驱动
├── ntc.c/h                      #   NTC 温度采样 + 故障检测
├── heat.c/h                     #   PWM 加热驱动
├── led.c/h                      #   LED 控制（含闪烁定时器）
├── key.c/h                      #   矩阵键盘 GPIO 扫描
├── buzzer.c/h                   #   蜂鸣器
└── bsp_rtc.c/h                  #   RTC 时钟
tools/                           # 工具库
├── pid.c/h                      #   工业级 PID 控制器
├── pwm_driver.c/h               #   PWM 封装
└── crc16.c/h                    #   Modbus CRC16 校验
```

### 层间通信

- **service 和 protocol 互不引用**，仅通过 `app_main.c` 注册的回调通信
- 模块间异步通知通过 `event_bus` 发布-订阅

```
app_main.c （唯一知道所有层的编排者）
    │
    ├── 注册 ble_service 帧回调 → 路由到 modbus_slave / at_parser
    ├── 注册 heat_service 上传回调 → 调用 protocol_upload_heating_status()
    ├── 注册 modbus_slave 读写回调 → 调用 heat_status_set/get 等
    └── 订阅 EVENT_KEY_* → 路由到 ble_mode / heat_xxx / music_xxx
```

## 通信协议

### Modbus RTU 寄存器映射

| 地址 | 名称 | 权限 | 范围 | 说明 |
|------|------|------|------|------|
| 0 | PowerSwitch | WO | 0-1 | 关机命令 |
| 1-2 | UTCTime | WO | 32-bit | UTC 时间戳 |
| 3-4 | AlarmSet | WO | 32-bit | 闹钟设置 |
| 5 | DeleteAlarm | WO | 1-2 | 删除闹钟 |
| 6 | ExecuteShortcut | WO | 1-2 | 执行快捷键 |
| 7 | HeatingStatus | RW | 0-1 | 加热开关 |
| 8 | HeatingLevel | RW | 0-2 | 档位 (35/45/55C) |
| 9 | HeatingTimer | RW | 0-720 | 定时分钟数 |
| 10-11 | ShortcutKey | RW | 0-65535 | 快捷键配置 |

### AT 命令

| 命令 | 功能 |
|------|------|
| `QM+00` / `QM+09` | 关闭蓝牙/音乐 LED |
| `QM+03` | 音乐模式 |
| `TS+00` | 蓝牙闪烁 |
| `TS+XX` | 蓝牙常亮 |

## 构建与烧录

### 前置条件

- STM32CubeCLT (GNU ARM Toolchain 14.3 + CMake 3.22 + Ninja)
- VS Code + CMake Tools 扩展
- OpenOCD + ST-Link

### 编译

```bash
cmake --preset Debug
cmake --build build/Debug
```

### 烧录

```bash
openocd -f openOCD.cfg -c "program build/Debug/LUNAR.elf verify reset exit"
```

或使用 `openocd_flash/download.bat`

### 资源占用

```
RAM:   ~15.5 KB / 20 KB  (75.9%)
FLASH: ~37.6 KB / 64 KB  (57.3%)
```

## 任务调度

| 任务 | 栈 | 优先级 | 周期 | 功能 |
|------|-----|--------|------|------|
| ble_task | 512B | 5 | 事件驱动 | BLE 帧分发 + 看门狗喂狗 |
| evt_disp | 256B | 4 | 事件驱动 | 事件总线分发 |
| heat_task | 512B | 3 | 500ms | PID 温控循环 |
| KeyScan | 256B | 2 | 20ms | 按键扫描 + 长短按识别 |

## 安全特性

- **IWDG** 独立看门狗（ble_task 喂狗）
- **NTC 故障检测** 开路/短路/卡死 自动停机
- **堆栈水位** 定期打印 FreeRTOS 栈余量
- **PID 保护** 输出限幅 + 抗积分饱和 + 微分先行
- **DMA 统计** 溢出计数可查询

## 分支

| 分支 | 说明 |
|------|------|
| `main` | 重构后分层架构 (service / protocol 分离) |
| `legacy` | 重构前原始代码 |
