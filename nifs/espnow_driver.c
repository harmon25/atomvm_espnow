//
// AtomVM ESPNOW Port Driver (ESP-IDF)
//
// This implements a port driver that:
// - Owns the ESPNOW resource lifecycle
// - Sends RX/TX events directly to the owner process (no polling)
// - Handles commands via native mailbox handler
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

#include <context.h>
#include <defaultatoms.h>
#include <globalcontext.h>
#include <interop.h>
#include <mailbox.h>
#include <port.h>
#include <scheduler.h>
#include <term.h>

#include "atomvm_espnow.h"

static const char *TAG = "espnow_port";

// Atom strings
static const char *const espnow_atom_str = "\x6" "espnow";
static const char *const rx_atom_str = "\x2" "rx";
static const char *const tx_atom_str = "\x2" "tx";
static const char *const broadcast_atom_str = "\x9" "broadcast";
static const char *const send_atom_str = "\x4" "send";
static const char *const add_peer_atom_str = "\x8" "add_peer";

static const uint8_t broadcast_addr[ESP_NOW_ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static inline bool is_broadcast_addr(const uint8_t *addr)
{
    return addr && memcmp(addr, broadcast_addr, ESP_NOW_ETH_ALEN) == 0;
}

// Global state for the singleton ESPNOW port
static espnow_port_data_t *s_port_data = NULL;
static GlobalContext *s_global = NULL;

// Queue for events from ISR context to be processed in main loop
typedef struct {
    enum { EVENT_RX, EVENT_TX } type;
    union {
        struct {
            uint8_t src_addr[ESP_NOW_ETH_ALEN];
            size_t len;
            uint8_t *data;  // heap allocated
        } rx;
        struct {
            bool is_broadcast;
            uint8_t dst_addr[ESP_NOW_ETH_ALEN];
            int status;
        } tx;
    };
} espnow_event_t;

static QueueHandle_t s_event_queue = NULL;

//
// ESP-NOW Callbacks - these run in WiFi task context
//

static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
    if (!recv_info || !recv_info->src_addr || !data || data_len < 0 || !s_event_queue) {
        return;
    }

    if (data_len > ESP_NOW_MAX_DATA_LEN_V2) {
        ESP_LOGW(TAG, "RX len too large: %d", data_len);
        return;
    }

    // Allocate data copy
    uint8_t *data_copy = malloc((size_t)data_len);
    if (!data_copy) {
        ESP_LOGW(TAG, "RX drop: no mem");
        return;
    }
    memcpy(data_copy, data, (size_t)data_len);

    espnow_event_t event = {
        .type = EVENT_RX,
        .rx = {
            .len = (size_t)data_len,
            .data = data_copy
        }
    };
    memcpy(event.rx.src_addr, recv_info->src_addr, ESP_NOW_ETH_ALEN);

    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX drop: queue full");
        free(data_copy);
        return;
    }

    ESP_LOGD(TAG, "RX from %02x:%02x:%02x:%02x:%02x:%02x len=%d",
        recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
        recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5],
        data_len);
}

static void send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (!s_event_queue) {
        return;
    }

    const uint8_t *mac_addr = (tx_info && tx_info->des_addr) ? tx_info->des_addr : NULL;
    const int tx_status = tx_info ? (int)tx_info->tx_status : (int)status;

    espnow_event_t event = {
        .type = EVENT_TX,
        .tx = {
            .is_broadcast = is_broadcast_addr(mac_addr),
            .status = tx_status
        }
    };
    if (mac_addr) {
        memcpy(event.tx.dst_addr, mac_addr, ESP_NOW_ETH_ALEN);
    } else {
        memset(event.tx.dst_addr, 0, ESP_NOW_ETH_ALEN);
    }

    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "TX event drop: queue full");
    }

    ESP_LOGD(TAG, "TX status=%d", tx_status);
}

//
// WiFi/ESPNOW Initialization
//

static esp_err_t ensure_wifi_started(uint8_t channel)
{
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

static esp_err_t espnow_init(uint8_t channel)
{
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
        ESP_LOGW(TAG, "ESPNOW version %lu (need v2)", (unsigned long)version);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Add broadcast peer
    esp_now_peer_info_t broadcast_peer = { 0 };
    memcpy(broadcast_peer.peer_addr, broadcast_addr, ESP_NOW_ETH_ALEN);
    broadcast_peer.channel = 0;
    broadcast_peer.ifidx = WIFI_IF_STA;
    broadcast_peer.encrypt = false;
    esp_err_t peer_err = esp_now_add_peer(&broadcast_peer);
    if (peer_err != ESP_OK && peer_err != ESP_ERR_ESPNOW_EXIST) {
        return peer_err;
    }

    (void)esp_now_register_recv_cb(recv_cb);
    (void)esp_now_register_send_cb(send_cb);

    return ESP_OK;
}

//
// Process events from the queue and send to owner
//

static void process_espnow_events(GlobalContext *global)
{
    if (!s_event_queue || !s_port_data || s_port_data->owner_process_id == 0) {
        return;
    }

    espnow_event_t event;
    while (xQueueReceive(s_event_queue, &event, 0) == pdTRUE) {
        // Allocate heap for message
        // {espnow, rx, FromMac, Data} or {espnow, tx, To, Status}
        size_t heap_size = 64;
        if (event.type == EVENT_RX) {
            heap_size += (event.rx.len + 3) / sizeof(term) + TERM_BINARY_HEAP_SIZE(event.rx.len) + TERM_BINARY_HEAP_SIZE(ESP_NOW_ETH_ALEN);
        } else {
            heap_size += TERM_BINARY_HEAP_SIZE(ESP_NOW_ETH_ALEN);
        }

        Heap heap;
        if (UNLIKELY(memory_init_heap(&heap, heap_size) != MEMORY_GC_OK)) {
            if (event.type == EVENT_RX) {
                free(event.rx.data);
            }
            continue;
        }

        term msg;
        if (event.type == EVENT_RX) {
            // {espnow, rx, FromMac, Data}
            term espnow_atom = globalcontext_make_atom(global, espnow_atom_str);
            term rx_atom = globalcontext_make_atom(global, rx_atom_str);
            term from_bin = term_from_literal_binary(event.rx.src_addr, ESP_NOW_ETH_ALEN, &heap, global);
            term data_bin = term_from_literal_binary(event.rx.data, event.rx.len, &heap, global);
            free(event.rx.data);

            msg = term_alloc_tuple(4, &heap);
            term_put_tuple_element(msg, 0, espnow_atom);
            term_put_tuple_element(msg, 1, rx_atom);
            term_put_tuple_element(msg, 2, from_bin);
            term_put_tuple_element(msg, 3, data_bin);
        } else {
            // {espnow, tx, To, Status}
            term espnow_atom = globalcontext_make_atom(global, espnow_atom_str);
            term tx_atom = globalcontext_make_atom(global, tx_atom_str);
            term to_term;
            if (event.tx.is_broadcast) {
                to_term = globalcontext_make_atom(global, broadcast_atom_str);
            } else {
                to_term = term_from_literal_binary(event.tx.dst_addr, ESP_NOW_ETH_ALEN, &heap, global);
            }
            term status_term = term_from_int(event.tx.status);

            msg = term_alloc_tuple(4, &heap);
            term_put_tuple_element(msg, 0, espnow_atom);
            term_put_tuple_element(msg, 1, tx_atom);
            term_put_tuple_element(msg, 2, to_term);
            term_put_tuple_element(msg, 3, status_term);
        }

        globalcontext_send_message(global, s_port_data->owner_process_id, msg);
        memory_destroy_heap(&heap, global);
    }
}

//
// Port Native Handler - processes commands from Erlang
//

static NativeHandlerResult espnow_consume_mailbox(Context *ctx)
{
    Message *msg = mailbox_first(&ctx->mailbox);
    term message = msg->message;

    espnow_port_data_t *data = (espnow_port_data_t *)ctx->platform_data;

    // First, check for any pending ESPNOW events
    process_espnow_events(ctx->global);

    GenMessage gen_message;
    GenMessageParseResult result = port_parse_gen_message(message, &gen_message);

    if (result == GenCallMessage) {
        // Handle gen_server:call style messages
        term cmd = gen_message.req;

        if (term_is_tuple(cmd) && term_get_tuple_arity(cmd) >= 1) {
            term cmd_name = term_get_tuple_element(cmd, 0);

            // {send, To, Data}
            if (globalcontext_is_term_equal_to_atom_string(ctx->global, cmd_name, send_atom_str)) {
                if (term_get_tuple_arity(cmd) >= 3) {
                    term to_term = term_get_tuple_element(cmd, 1);
                    term data_term = term_get_tuple_element(cmd, 2);

                    if (!term_is_binary(data_term)) {
                        port_send_reply(ctx, gen_message.pid, gen_message.ref, 
                            port_create_error_tuple(ctx, BADARG_ATOM));
                        goto done;
                    }

                    const uint8_t *peer_addr = NULL;
                    if (term_is_atom(to_term)) {
                        if (globalcontext_is_term_equal_to_atom_string(ctx->global, to_term, broadcast_atom_str)) {
                            peer_addr = NULL;  // broadcast
                        } else {
                            port_send_reply(ctx, gen_message.pid, gen_message.ref,
                                port_create_error_tuple(ctx, BADARG_ATOM));
                            goto done;
                        }
                    } else if (term_is_binary(to_term) && term_binary_size(to_term) == ESP_NOW_ETH_ALEN) {
                        peer_addr = (const uint8_t *)term_binary_data(to_term);
                    } else {
                        port_send_reply(ctx, gen_message.pid, gen_message.ref,
                            port_create_error_tuple(ctx, BADARG_ATOM));
                        goto done;
                    }

                    const uint8_t *send_data = (const uint8_t *)term_binary_data(data_term);
                    size_t send_len = term_binary_size(data_term);
                    const uint8_t *dst = peer_addr ? peer_addr : broadcast_addr;

                    esp_err_t err = esp_now_send(dst, send_data, send_len);
                    if (err != ESP_OK) {
                        port_send_reply(ctx, gen_message.pid, gen_message.ref,
                            port_create_error_tuple(ctx, term_from_int(err)));
                    } else {
                        port_send_reply(ctx, gen_message.pid, gen_message.ref, OK_ATOM);
                    }
                    goto done;
                }
            }

            // {add_peer, Mac, Channel}
            if (globalcontext_is_term_equal_to_atom_string(ctx->global, cmd_name, add_peer_atom_str)) {
                if (term_get_tuple_arity(cmd) >= 3) {
                    term mac_term = term_get_tuple_element(cmd, 1);
                    term channel_term = term_get_tuple_element(cmd, 2);

                    if (!term_is_binary(mac_term) || term_binary_size(mac_term) != ESP_NOW_ETH_ALEN) {
                        port_send_reply(ctx, gen_message.pid, gen_message.ref,
                            port_create_error_tuple(ctx, BADARG_ATOM));
                        goto done;
                    }

                    if (!term_is_integer(channel_term)) {
                        port_send_reply(ctx, gen_message.pid, gen_message.ref,
                            port_create_error_tuple(ctx, BADARG_ATOM));
                        goto done;
                    }

                    const uint8_t *peer_addr = (const uint8_t *)term_binary_data(mac_term);
                    uint8_t channel = (uint8_t)term_to_int(channel_term);

                    esp_now_peer_info_t peer = { 0 };
                    memcpy(peer.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
                    peer.channel = channel;
                    peer.ifidx = WIFI_IF_STA;
                    peer.encrypt = false;

                    esp_err_t err = esp_now_add_peer(&peer);
                    if (err == ESP_ERR_ESPNOW_EXIST) {
                        err = ESP_OK;
                    }

                    if (err != ESP_OK) {
                        port_send_reply(ctx, gen_message.pid, gen_message.ref,
                            port_create_error_tuple(ctx, term_from_int(err)));
                    } else {
                        port_send_reply(ctx, gen_message.pid, gen_message.ref, OK_ATOM);
                    }
                    goto done;
                }
            }
        }

        // Unknown command
        port_send_reply(ctx, gen_message.pid, gen_message.ref,
            port_create_error_tuple(ctx, BADARG_ATOM));
    }

done:
    mailbox_remove_message(&ctx->mailbox, &ctx->heap);
    
    // Process any events that came in while handling the command
    process_espnow_events(ctx->global);
    
    return NativeContinue;
}

//
// Port Driver Interface
//

Context *atomvm_espnow_create_port(GlobalContext *global, term opts)
{
    if (s_port_data != NULL) {
        ESP_LOGE(TAG, "ESPNOW port already exists (singleton)");
        return NULL;
    }

    // Parse options: [{channel, N}, {owner, Pid}]
    uint8_t channel = 0;
    int32_t owner_pid = 0;

    term channel_term = interop_kv_get_value_default(opts, ATOM_STR("\x7", "channel"), term_from_int(0), global);
    if (term_is_integer(channel_term)) {
        channel = (uint8_t)term_to_int(channel_term);
    }

    term owner_term = interop_kv_get_value(opts, ATOM_STR("\x5", "owner"), global);
    if (term_is_pid(owner_term)) {
        owner_pid = term_to_local_process_id(owner_term);
    }

    // Initialize ESPNOW
    esp_err_t err = espnow_init(channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "espnow_init failed: %d", (int)err);
        return NULL;
    }

    // Create event queue
    s_event_queue = xQueueCreate(32, sizeof(espnow_event_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return NULL;
    }

    // Create port context
    Context *ctx = context_new(global);
    if (!ctx) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return NULL;
    }

    // Allocate port data
    espnow_port_data_t *port_data = calloc(1, sizeof(espnow_port_data_t));
    if (!port_data) {
        context_destroy(ctx);
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return NULL;
    }

    port_data->initialized = true;
    port_data->channel = channel;
    port_data->owner_process_id = owner_pid;
    port_data->global = global;

    ctx->native_handler = espnow_consume_mailbox;
    ctx->platform_data = port_data;

    s_port_data = port_data;
    s_global = global;

    ESP_LOGI(TAG, "ESPNOW port created (channel=%d, owner=%d)", (int)channel, (int)owner_pid);

    return ctx;
}

void atomvm_espnow_init(GlobalContext *global)
{
    UNUSED(global);
    ESP_LOGI(TAG, "ESPNOW port driver registered");
}

void atomvm_espnow_destroy(GlobalContext *global)
{
    UNUSED(global);
    
    if (s_event_queue) {
        espnow_event_t event;
        while (xQueueReceive(s_event_queue, &event, 0) == pdTRUE) {
            if (event.type == EVENT_RX && event.rx.data) {
                free(event.rx.data);
            }
        }
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }

    if (s_port_data) {
        free(s_port_data);
        s_port_data = NULL;
    }

    s_global = NULL;

    esp_now_deinit();
    ESP_LOGI(TAG, "ESPNOW port driver destroyed");
}
