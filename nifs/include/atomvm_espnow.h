//
// AtomVM ESPNOW Port Driver Interface
//

#ifndef ATOMVM_ESPNOW_H
#define ATOMVM_ESPNOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_now.h"

#include <globalcontext.h>
#include <context.h>

/**
 * @brief ESPNOW port driver data stored in Context->platform_data
 */
typedef struct espnow_port_data {
    bool initialized;
    uint8_t channel;
    int32_t owner_process_id;  // Process to receive RX/TX events
    GlobalContext *global;
} espnow_port_data_t;

/**
 * @brief Initialize the ESPNOW port driver (called once at AtomVM startup)
 */
void atomvm_espnow_init(GlobalContext *global);

/**
 * @brief Destroy the ESPNOW port driver (called at AtomVM shutdown)
 */
void atomvm_espnow_destroy(GlobalContext *global);

/**
 * @brief Create a new ESPNOW port context
 */
Context *atomvm_espnow_create_port(GlobalContext *global, term opts);

/**
 * @brief Send a message to the owner process from ESPNOW callbacks
 */
void espnow_send_message_to_owner(GlobalContext *global, int32_t owner_pid, term msg);

#endif
