/**
 * @file key.c
 * @brief 矩阵键盘扫描驱动（初始化与扫描分离）
 * @version 1.2
 */

#include "key.h"

/*============================================================================
 * 矩阵键盘硬件配置（编译期常量）
 *============================================================================*/
typedef struct {
    uint16_t output_pin;       // 行驱动输出引脚
    uint16_t input_pins[5];    // 列输入引脚列表（0=无效）
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

/*============================================================================
 * 初始化（仅调用一次，所有引脚设为上拉输入——最安全默认状态）
 *============================================================================*/
void key_init(void)
{
    GPIO_InitTypeDef cfg = {0};
    cfg.Mode  = GPIO_MODE_INPUT;
    cfg.Pull  = GPIO_PULLUP;

    /* 收集所有用到的引脚并统一初始化为上拉输入 */
    uint16_t pins_b = 0, pins_a = 0;
    for (uint8_t r = 0; r < KEY_ROW_COUNT; r++) {
        pins_b |= key_rows[r].output_pin;
        for (uint8_t c = 0; c < 5; c++) {
            uint16_t p = key_rows[r].input_pins[c];
            if (p == GPIO_PIN_15) pins_a |= p;
            else if (p)            pins_b |= p;
        }
    }
    if (pins_b) { cfg.Pin = pins_b; HAL_GPIO_Init(GPIOB, &cfg); }
    if (pins_a) { cfg.Pin = pins_a; HAL_GPIO_Init(GPIOA, &cfg); }

    /* 释放所有行（已在 INPUT_PULLUP 状态，高电平） */
    for (uint8_t r = 0; r < KEY_ROW_COUNT; r++)
        HAL_GPIO_WritePin(GPIOB, key_rows[r].output_pin, GPIO_PIN_SET);
}

/*============================================================================
 * 扫描（每行仅切换一次输出引脚模式）
 *============================================================================*/
unsigned char get_key(void)
{
    if (HAL_GPIO_ReadPin(POWER_DC_GPIO_Port, POWER_DC_Pin) == GPIO_PIN_RESET)
        return KEY_POWER;

    uint8_t mode = KEY_NULL;
    GPIO_InitTypeDef cfg = {0};

    for (uint8_t row = 0; row < KEY_ROW_COUNT; row++) {
        const key_row_config_t *r = &key_rows[row];

        /* 将当前行引脚临时切为推挽输出，拉低 */
        cfg.Pin   = r->output_pin;
        cfg.Mode  = GPIO_MODE_OUTPUT_PP;
        cfg.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &cfg);
        HAL_GPIO_WritePin(GPIOB, r->output_pin, GPIO_PIN_RESET);

        /* 读取所有列 */
        for (uint8_t col = 0; col < 5; col++) {
            uint16_t pin = r->input_pins[col];
            if (pin == 0) continue;
            GPIO_TypeDef *port = (pin == GPIO_PIN_15) ? GPIOA : GPIOB;
            if (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET)
                mode = r->key_values[col];
        }

        /* 恢复为输入上拉（释放） */
        cfg.Pin  = r->output_pin;
        cfg.Mode = GPIO_MODE_INPUT;
        cfg.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &cfg);
    }

    return mode;
}
