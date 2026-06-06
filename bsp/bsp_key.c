/**
 * @file    bsp_key.c
 * @brief   矩阵键盘驱动实现（静态分配）
 * @version 2.1
 */

#include "bsp_key.h"

struct bsp_key_s {
    const bsp_key_row_t*       rows;
    uint8_t                    row_count;
    const bsp_key_power_pin_t* power_pin;
    GPIO_TypeDef*              row_port;
    bool                       initialized;
};

static struct bsp_key_s s_inst;
static bool s_inited = false;

static void key_init_gpio_pins(void)
{
    GPIO_InitTypeDef cfg = {0};
    cfg.Mode = GPIO_MODE_INPUT;
    cfg.Pull = GPIO_PULLUP;

    uint16_t pins_b = 0, pins_a = 0;
    for (uint8_t r = 0; r < s_inst.row_count; r++) {
        pins_b |= s_inst.rows[r].output_pin;
        for (uint8_t c = 0; c < 5; c++) {
            uint16_t p = s_inst.rows[r].input_pins[c];
            if (p == GPIO_PIN_15) pins_a |= p;
            else if (p) pins_b |= p;
        }
    }
    if (pins_b) { cfg.Pin = pins_b; HAL_GPIO_Init(GPIOB, &cfg); }
    if (pins_a) { cfg.Pin = pins_a; HAL_GPIO_Init(GPIOA, &cfg); }

    for (uint8_t r = 0; r < s_inst.row_count; r++)
        HAL_GPIO_WritePin(s_inst.row_port, s_inst.rows[r].output_pin, GPIO_PIN_SET);
}

bsp_status_t bsp_key_init(bsp_key_t** handle, const bsp_key_config_t* config)
{
    if (!handle || !config || !config->rows || config->row_count == 0) return BSP_ERR_PARAM;
    if (*handle || s_inited) return BSP_ERR_BUSY;

    s_inst.rows       = config->rows;
    s_inst.row_count  = config->row_count;
    s_inst.power_pin  = config->power_pin;
    s_inst.row_port   = config->row_port ? config->row_port : GPIOB;
    s_inst.initialized = true;
    s_inited = true;

    key_init_gpio_pins();
    *handle = (bsp_key_t*)&s_inst;
    return BSP_OK;
}

void bsp_key_deinit(bsp_key_t** handle)
{
    if (!handle || !*handle || !s_inited) return;
    s_inst.initialized = false;
    s_inited = false;
    *handle = NULL;
}

bsp_status_t bsp_key_scan(bsp_key_t* handle, uint8_t* key_value)
{
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    if (!key_value) return BSP_ERR_PARAM;

    uint8_t mode = 0;
    GPIO_InitTypeDef cfg = {0};

    for (uint8_t row = 0; row < s_inst.row_count; row++) {
        const bsp_key_row_t* r = &s_inst.rows[row];
        cfg.Pin   = r->output_pin;
        cfg.Mode  = GPIO_MODE_OUTPUT_PP;
        cfg.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(s_inst.row_port, &cfg);
        HAL_GPIO_WritePin(s_inst.row_port, r->output_pin, GPIO_PIN_RESET);

        for (uint8_t col = 0; col < 5; col++) {
            uint16_t pin = r->input_pins[col];
            if (!pin) continue;
            GPIO_TypeDef* port = (pin == GPIO_PIN_15) ? GPIOA : GPIOB;
            if (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET)
                mode = r->key_values[col];
        }

        cfg.Pin  = r->output_pin;
        cfg.Mode = GPIO_MODE_INPUT;
        cfg.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(s_inst.row_port, &cfg);
    }

    *key_value = mode;
    return BSP_OK;
}

bool bsp_key_is_power_pressed(bsp_key_t* handle)
{
    if (!handle || !s_inited || !s_inst.power_pin) return false;
    return (HAL_GPIO_ReadPin(s_inst.power_pin->port, s_inst.power_pin->pin) == GPIO_PIN_RESET);
}
