/**
 * @file key.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-09-19
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "key.h"

unsigned char get_key(void)
{
    if (HAL_GPIO_ReadPin(POWER_DC_GPIO_Port, POWER_DC_Pin) == 0)
    {
        return 18; // 电源键
    }

    uint8_t          Mode = 0;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // 定义每一行的配置信息
    typedef struct
    {
        uint16_t outputPin;     // 输出引脚
        uint16_t inputPins[5];  // 输入引脚列表
        uint8_t  modeValues[5]; // 对应的按键模式值
    } KeyRowConfig;

    // 配置每一行的键盘设置
    KeyRowConfig rows[] = {{GPIO_PIN_9, {GPIO_PIN_8, GPIO_PIN_3, GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15}, {1, 2, 3, 4, 5}},
                           {GPIO_PIN_8, {GPIO_PIN_3, GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15, 0}, {6, 7, 8, 9, 0}},
                           {GPIO_PIN_3, {GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15, 0, 0}, {12, 10, 11, 0, 0}},
                           {GPIO_PIN_5, {GPIO_PIN_4, GPIO_PIN_15, 0, 0, 0}, {14, 13, 0, 0, 0}},
                           {GPIO_PIN_4, {GPIO_PIN_15, 0, 0, 0, 0}, {15, 0, 0, 0, 0}}};

    // 遍历每一行的配置
    for (uint8_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
    {
        // 配置输出引脚
        GPIO_InitStruct.Pin = rows[i].outputPin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        HAL_GPIO_WritePin(GPIOB, rows[i].outputPin, GPIO_PIN_RESET);

        // 配置输入引脚并检测按键
        for (uint8_t j = 0; j < 5; j++)
        {
            if (rows[i].inputPins[j] != 0)
            {
                GPIO_InitStruct.Pin = rows[i].inputPins[j];
                GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
                GPIO_InitStruct.Pull = GPIO_PULLUP;
                HAL_GPIO_Init((rows[i].inputPins[j] == GPIO_PIN_15) ? GPIOA : GPIOB, &GPIO_InitStruct);

                // 检测按键是否按下
                if (HAL_GPIO_ReadPin((rows[i].inputPins[j] == GPIO_PIN_15) ? GPIOA : GPIOB, rows[i].inputPins[j]) ==
                    GPIO_PIN_RESET)
                {
                    Mode = rows[i].modeValues[j];
                }
            }
        }

        // 释放当前行的输出引脚
        HAL_GPIO_WritePin(GPIOB, rows[i].outputPin, GPIO_PIN_SET);
    }

    return Mode;
}
