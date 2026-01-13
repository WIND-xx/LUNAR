// heat.h
#ifndef __HEAT_H
#define __HEAT_H

#include <stdint.h>

void heat_init(void);
void heat_deinit(void);
void heat_on(uint16_t power);   // power: 0~100 (%)
void heat_off(void);

#endif /* __HEAT_H */
