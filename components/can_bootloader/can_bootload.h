#ifndef CAN_BOOTLOAD_H
#define CAN_BOOTLOAD_H

#include "driver/twai.h"

void Bootload_init(void);

void Bootload_rx(twai_message_t msg);

#endif