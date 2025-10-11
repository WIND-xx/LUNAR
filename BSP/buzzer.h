// buzzer.h
#ifndef __BUZZER_H
#define __BUZZER_H

#include <stdbool.h>

void buzzer_init(void);
void buzzer_set(bool on); // true = 响，false = 关

#endif
