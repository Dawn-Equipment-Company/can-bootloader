#ifndef CAN_BOOTLOAD_H
#define CAN_BOOTLOAD_H

#include "driver/twai.h"

typedef enum UpdateState_E {
    UPDATE_Idle,
    UPDATE_InProgress,
    UPDATE_Complete,
} UpdateState;

void Bootload_init(uint32_t addr);

/// @brief Get the current state of the bootload operation.  Useful if you want to shut down outputs etc while bootloading
/// @param  
/// @return 
UpdateState Bootload_current_state(void);

/// @brief Call this with incoming TWAI messages to run the bootload update process
/// @param msg 
void Bootload_rx(twai_message_t msg);

/// @brief Bootloading status task.  Optional. Transmits current bootloading status on CAN
/// @param pvParameters 
void Bootload_task(void *pvParameters);

#endif