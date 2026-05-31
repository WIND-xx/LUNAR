// buzzer.h
#ifndef __BUZZER_H
#define __BUZZER_H

#include <stdbool.h>
#include <stdint.h>

void buzzer_init(void);
void buzzer_set(bool on);               // true = 响，false = 关
bool buzzer_beep(uint32_t duration_ms); // 蜂鸣指定时长（毫秒）
#endif /* __BUZZER_H */
