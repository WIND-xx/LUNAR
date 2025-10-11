#ifndef __BT401_H
#define __BT401_H

#include <stdint.h>

void bt401_init(void);

// 原始读写
uint8_t  bt401_readbyte(uint8_t *rx_byte);
uint16_t bt401_readbytes(uint8_t *buf, uint16_t len);
uint8_t  bt401_sendbytes(uint8_t *buf, uint16_t len);
int      bt401_printf(const char *format, ...);

#endif /* __BT401_H */
