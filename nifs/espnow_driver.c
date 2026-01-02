//
// Minimal ESPNOW driver skeleton for AtomVM (ESP-IDF)
//

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "sdkconfig.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "atomvm_espnow.h"

struct avm_espnow_handle {
    bool initialized;
    uint8_t channel;
    QueueHandle_t rx_queue;
    QueueHandle_t tx_queue;
};

static const char *TAG = "atomvm_espnow";

static const uint8_t broadcast_addr[ESP_NOW_ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static inline bool is_broadcast_addr(const uint8_t *addr)
{
    return addr && memcmp(addr, broadcast_addr, ESP_NOW_ETH_ALEN) == 0;
}

// Current implementation supports a single active handle.
static avm_espnow_handle_t *s_handle = NULL;

static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
    if (!recv_info || !recv_info->src_addr || !data || data_len < 0) {
        return;
    }

    avm_espnow_handle_t *handle = s_handle;
    if (!handle || !handle->rx_queue) {
        return;
    }

    if (data_len > ESP_NOW_MAX_DATA_LEN_V2) {
        ESP_LOGW(TAG, "RX len too large: %d", data_len);
        return;
    }

    avm_espnow_rx_t *rx = malloc(sizeof(avm_espnow_rx_t) + (size_t) data_len);
    if (!rx) {
        ESP_LOGW(TAG, "RX drop: no mem");
        return;
    }
    memcpy(rx->src_addr, recv_info->src_addr, ESP_NOW_ETH_ALEN);
    rx->len = (size_t) data_len;
    memcpy(rx->data, data, (size_t) data_len);

    // Non-blocking enqueue; drop if full.
    if (xQueueSend(handle->rx_queue, &rx, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX drop: queue full");
        free(rx);
        return;
    }

    ESP_LOGI(TAG, "RX from %02x:%02x:%02x:%02x:%02x:%02x len=%d",
        recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
        recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5],
        data_len);
}

static void send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    const uint8_t *mac_addr = (tx_info && tx_info->des_addr) ? tx_info->des_addr : NULL;
    const int tx_status = tx_info ? (int) tx_info->tx_status : (int) status;

    avm_espnow_handle_t *handle = s_handle;
    if (handle && handle->tx_queue) {
        avm_espnow_tx_t *tx = malloc(sizeof(avm_espnow_tx_t));
        if (tx) {
            tx->is_broadcast = is_broadcast_addr(mac_addr);
            if (mac_addr) {
                memcpy(tx->dst_addr, mac_addr, ESP_NOW_ETH_ALEN);
            } else {
                memset(tx->dst_addr, 0, ESP_NOW_ETH_ALEN);
            }
            tx->status = tx_status;

            if (xQueueSend(handle->tx_queue, &tx, 0) != pdTRUE) {
                free(tx);
            }
        }
    }

    if (is_broadcast_addr(mac_addr)) {
        ESP_LOGI(TAG, "TX broadcast status=%d", tx_status);
        return;
    }
    ESP_LOGI(TAG, "TX to %02x:%02x:%02x:%02x:%02x:%02x status=%d",
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        tx_status);
}

static esp_err_t ensure_wifi_started(uint8_t channel)
{
    // NVS is required by WiFi/ESPNOW.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (channel != 0) {
        err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t avm_espnow_new(const avm_espnow_config_t *config, avm_espnow_handle_t **out_handle)
{
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;

    if (s_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t channel = 0;
    if (config) {
        channel = config->channel;
    }
    if (channel > 14) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_wifi_started(channel);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_now_init();
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        return err;
    }

    uint32_t version = 0;
    err = esp_now_get_version(&version);
    if (err != ESP_OK) {
        return err;
    }
    if (version < 2) {
        ESP_LOGW(TAG, "ESPNOW version %lu (need v2)", (unsigned long) version);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Ensure broadcast peer exists so sending to FF:FF:FF:FF:FF:FF works.
    esp_now_peer_info_t broadcast_peer = { 0 };
    memcpy(broadcast_peer.peer_addr, broadcast_addr, ESP_NOW_ETH_ALEN);
    broadcast_peer.channel = 0;
    broadcast_peer.ifidx = WIFI_IF_STA;
    broadcast_peer.encrypt = false;
    esp_err_t peer_err = esp_now_add_peer(&broadcast_peer);
    if (peer_err != ESP_OK && peer_err != ESP_ERR_ESPNOW_EXIST) {
        return peer_err;
    }

    // Register callbacks (best-effort; ok if already registered).
    (void) esp_now_register_recv_cb(recv_cb);
    (void) esp_now_register_send_cb(send_cb);

    avm_espnow_handle_t *handle = calloc(1, sizeof(avm_espnow_handle_t));
    if (!handle) {
        return ESP_ERR_NO_MEM;
    }

    handle->rx_queue = xQueueCreate(16, sizeof(void *));
    if (!handle->rx_queue) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    handle->tx_queue = xQueueCreate(16, sizeof(void *));
    if (!handle->tx_queue) {
        vQueueDelete(handle->rx_queue);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    handle->initialized = true;
    handle->channel = channel;

    s_handle = handle;
    *out_handle = handle;
    return ESP_OK;
}

esp_err_t avm_espnow_del(avm_espnow_handle_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->rx_queue) {
        void *ptr = NULL;
        while (xQueueReceive(handle->rx_queue, &ptr, 0) == pdTRUE) {
            free(ptr);
        }
        vQueueDelete(handle->rx_queue);
        handle->rx_queue = NULL;
    }

    if (handle->tx_queue) {
        void *ptr = NULL;
        while (xQueueReceive(handle->tx_queue, &ptr, 0) == pdTRUE) {
            free(ptr);
        }
        vQueueDelete(handle->tx_queue);
        handle->tx_queue = NULL;
    }

    if (s_handle == handle) {
        s_handle = NULL;
    }

    // Best-effort deinit; a future implementation may refcount or centralize init.
    (void) esp_now_deinit();

    free(handle);
    return ESP_OK;
}

esp_err_t avm_espnow_recv(avm_espnow_handle_t *handle, avm_espnow_rx_t **out_rx)
{
    if (!out_rx) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_rx = NULL;

    if (!handle || !handle->initialized || !handle->rx_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    void *ptr = NULL;
    if (xQueueReceive(handle->rx_queue, &ptr, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out_rx = (avm_espnow_rx_t *) ptr;
    return ESP_OK;
}

void avm_espnow_rx_free(avm_espnow_rx_t *rx)
{
    free(rx);
}

esp_err_t avm_espnow_poll(avm_espnow_handle_t *handle, avm_espnow_rx_t **out_rx, avm_espnow_tx_t **out_tx)
{
    if (!out_rx || !out_tx) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_rx = NULL;
    *out_tx = NULL;

    if (!handle || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Prefer RX over TX.
    if (handle->rx_queue) {
        void *ptr = NULL;
        if (xQueueReceive(handle->rx_queue, &ptr, 0) == pdTRUE) {
            *out_rx = (avm_espnow_rx_t *) ptr;
            return ESP_OK;
        }
    }

    if (handle->tx_queue) {
        void *ptr = NULL;
        if (xQueueReceive(handle->tx_queue, &ptr, 0) == pdTRUE) {
            *out_tx = (avm_espnow_tx_t *) ptr;
            return ESP_OK;
        }
    }

    return ESP_ERR_TIMEOUT;
}

void avm_espnow_tx_free(avm_espnow_tx_t *tx)
{
    free(tx);
}

esp_err_t avm_espnow_add_peer(avm_espnow_handle_t *handle, const uint8_t peer_addr[ESP_NOW_ETH_ALEN], uint8_t channel)
{
    if (!handle || !handle->initialized || !peer_addr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (channel > 14) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
    peer.channel = channel; // 0 means current channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer);
    if (err == ESP_ERR_ESPNOW_EXIST) {
        return ESP_OK;
    }
    return err;
}

esp_err_t avm_espnow_send(avm_espnow_handle_t *handle, const uint8_t *peer_addr_or_null, const uint8_t *data, size_t len)
{
    if (!handle || !handle->initialized || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > ESP_NOW_MAX_DATA_LEN_V2) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *dst = peer_addr_or_null ? peer_addr_or_null : broadcast_addr;
    return esp_now_send(dst, data, len);
}
