/**
 * @file key.c
 * @brief 矩阵键盘扫描驱动
 * @version 1.1
 */

#include "key.h"

/* 矩阵键盘行/列配置（编译期常量） */
typedef struct {
    uint16_t output_pin;       // 行驱动输出引脚
    uint16_t input_pins[5];    // 列输入引脚列表（0表示无效）
    uint8_t  key_values[5];    // 对应按键返回值
} key_row_config_t;

static const key_row_config_t key_rows[] = {
    {GPIO_PIN_9, {GPIO_PIN_8, GPIO_PIN_3, GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15}, {1, 2, 3, 4, 5}},
    {GPIO_PIN_8, {GPIO_PIN_3, GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15, 0},          {6, 7, 8, 9, 0}},
    {GPIO_PIN_3, {GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15, 0, 0},                    {12, 10, 11, 0, 0}},
    {GPIO_PIN_5, {GPIO_PIN_4, GPIO_PIN_15, 0, 0, 0},                             {14, 13, 0, 0, 0}},
    {GPIO_PIN_4, {GPIO_PIN_15, 0, 0, 0, 0},                                      {15, 0, 0, 0, 0}},
};

#define KEY_ROW_COUNT (sizeof(key_rows) / sizeof(key_rows[0]))

unsigned char get_key(void)
{
    if (HAL_GPIO_ReadPin(POWER_DC_GPIO_Port, POWER_DC_Pin) == 0) {
        return KEY_POWER;
    }

    uint8_t          mode = KEY_NULL;
    GPIO_InitTypeDef gpio_init = {0};

    for (uint8_t row = 0; row < KEY_ROW_COUNT; row++) {
        const key_row_config_t *cfg = &key_rows[row];

        // 配置当前行为输出低电平
        gpio_init.Pin   = cfg->output_pin;
        gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
        gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &gpio_init);
        HAL_GPIO_WritePin(GPIOB, cfg->output_pin, GPIO_PIN_RESET);

        // 扫描该行的所有列
        for (uint8_t col = 0; col < 5; col++) {
            uint16_t in_pin = cfg->input_pins[col];
            if (in_pin == 0) continue;

            GPIO_TypeDef *port = (in_pin == GPIO_PIN_15) ? GPIOA : GPIOB;

            gpio_init.Pin  = in_pin;
            gpio_init.Mode = GPIO_MODE_INPUT;
            gpio_init.Pull = GPIO_PULLUP;
            HAL_GPIO_Init(port, &gpio_init);

            if (HAL_GPIO_ReadPin(port, in_pin) == GPIO_PIN_RESET) {
                mode = cfg->key_values[col];
            }
        }

        // 释放当前行
        HAL_GPIO_WritePin(GPIOB, cfg->output_pin, GPIO_PIN_SET);
    }

    return mode;
}
