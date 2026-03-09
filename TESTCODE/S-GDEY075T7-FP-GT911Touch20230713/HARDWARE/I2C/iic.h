#ifndef __IIC_H__
#define __IIC_H__
#include "stm32f10x.h"

#define E_Ok        0
#define E_Error     1

void iic_start(void);
void iic_stop(void);
void iic_ack(void);
void iic_nack(void);
uint8_t iic_wait_ack(void);
uint8_t iic_write_byte(uint8_t dat, uint8_t cack);
uint8_t iic_read_byte(uint8_t ack);
void iic_init(void);


#endif


