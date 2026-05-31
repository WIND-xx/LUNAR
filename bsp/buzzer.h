// buzzer.h
#ifndef __BUZZER_H
#define __BUZZER_H

#include <stdbool.h>
#include <stdint.h>

void buzzer_init(void);
void buzzer_set(bool on);
bool buzzer_beep(uint32_t duration_ms);

#endif
