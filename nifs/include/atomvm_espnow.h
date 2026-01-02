//
// AtomVM ESPNOW interface (internal)
//

#ifndef ATOMVM_ESPNOW_H
#define ATOMVM_ESPNOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_now.h"

typedef struct avm_espnow_handle avm_espnow_handle_t;

typedef struct {
    // 0 means "do not change channel"; otherwise 1..14.
    uint8_t channel;
} avm_espnow_config_t;

esp_err_t avm_espnow_new(const avm_espnow_config_t *config, avm_espnow_handle_t **out_handle);
esp_err_t avm_espnow_del(avm_espnow_handle_t *handle);

esp_err_t avm_espnow_add_peer(avm_espnow_handle_t *handle, const uint8_t peer_addr[ESP_NOW_ETH_ALEN], uint8_t channel);
esp_err_t avm_espnow_send(avm_espnow_handle_t *handle, const uint8_t *peer_addr_or_null, const uint8_t *data, size_t len);

typedef struct {
    uint8_t src_addr[ESP_NOW_ETH_ALEN];
    size_t len;
    uint8_t data[];
} avm_espnow_rx_t;

typedef struct {
    bool is_broadcast;
    uint8_t dst_addr[ESP_NOW_ETH_ALEN];
    int status; // esp_now_send_status_t
} avm_espnow_tx_t;

// Non-blocking receive. Returns ESP_OK and sets *out_rx if a frame is available.
// Returns ESP_ERR_TIMEOUT if no frame is available.
esp_err_t avm_espnow_recv(avm_espnow_handle_t *handle, avm_espnow_rx_t **out_rx);
void avm_espnow_rx_free(avm_espnow_rx_t *rx);

// Unified non-blocking poll.
// Returns ESP_OK with one of out_rx/out_tx set.
// Returns ESP_ERR_TIMEOUT if no event.
esp_err_t avm_espnow_poll(avm_espnow_handle_t *handle, avm_espnow_rx_t **out_rx, avm_espnow_tx_t **out_tx);
void avm_espnow_tx_free(avm_espnow_tx_t *tx);

#endif
