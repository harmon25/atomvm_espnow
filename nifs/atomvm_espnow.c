//
// AtomVM ESPNOW NIF collection (skeleton)
//

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <context.h>
#include <defaultatoms.h>
#include <nifs.h>
#include <portnifloader.h>
#include <term.h>

#include "atomvm_espnow.h"

static const char *const not_supported_atom = "\xD" "not_supported";
static const char *const broadcast_atom = "\x9" "broadcast";
static const char *const busy_atom = "\x4" "busy";
static const char *const none_atom = "\x4" "none";
static const char *const rx_atom = "\x2" "rx";
static const char *const tx_atom = "\x2" "tx";

static inline term ptr_to_binary(void *ptr, Context *ctx)
{
    return term_from_literal_binary(&ptr, sizeof(void *), &ctx->heap, ctx->global);
}

static inline void *binary_to_ptr(term binary)
{
    if (term_binary_size(binary) != sizeof(void *)) {
        return NULL;
    }
    const char *ptr = term_binary_data(binary);
    return *((void **) ptr);
}

static term make_error_tuple(Context *ctx, term reason)
{
    if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    term error_tuple = term_alloc_tuple(2, &ctx->heap);
    term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
    term_put_tuple_element(error_tuple, 1, reason);
    return error_tuple;
}

static term nif_init(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term channel_term = argv[0];
    VALIDATE_VALUE(channel_term, term_is_integer);

    avm_int_t channel = term_to_int(channel_term);
    if (channel < 0 || channel > 14) {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    avm_espnow_config_t config = {
        .channel = (uint8_t) channel
    };

    avm_espnow_handle_t *handle = NULL;
    esp_err_t err = avm_espnow_new(&config, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_STATE) {
            return make_error_tuple(ctx, globalcontext_make_atom(ctx->global, busy_atom));
        }
        return make_error_tuple(ctx, term_from_int(err));
    }

    return ptr_to_binary(handle, ctx);
}

static term nif_deinit(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle_bin = argv[0];
    VALIDATE_VALUE(handle_bin, term_is_binary);

    avm_espnow_handle_t *handle = (avm_espnow_handle_t *) binary_to_ptr(handle_bin);
    if (!handle) {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    esp_err_t err = avm_espnow_del(handle);
    if (err != ESP_OK) {
        return make_error_tuple(ctx, term_from_int(err));
    }
    return OK_ATOM;
}

static term nif_add_peer(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle_bin = argv[0];
    VALIDATE_VALUE(handle_bin, term_is_binary);
    term mac_bin = argv[1];
    VALIDATE_VALUE(mac_bin, term_is_binary);
    term channel_term = argv[2];
    VALIDATE_VALUE(channel_term, term_is_integer);

    if (term_binary_size(mac_bin) != ESP_NOW_ETH_ALEN) {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    avm_int_t channel = term_to_int(channel_term);
    if (channel < 0 || channel > 14) {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    avm_espnow_handle_t *handle = (avm_espnow_handle_t *) binary_to_ptr(handle_bin);
    if (!handle) {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    const uint8_t *peer_addr = (const uint8_t *) term_binary_data(mac_bin);

    esp_err_t err = avm_espnow_add_peer(handle, peer_addr, (uint8_t) channel);
    if (err != ESP_OK) {
        return make_error_tuple(ctx, term_from_int(err));
    }

    return OK_ATOM;
}

static term nif_send(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle_bin = argv[0];
    VALIDATE_VALUE(handle_bin, term_is_binary);
    term to_term = argv[1];
    term data_bin = argv[2];
    VALIDATE_VALUE(data_bin, term_is_binary);

    avm_espnow_handle_t *handle = (avm_espnow_handle_t *) binary_to_ptr(handle_bin);
    if (!handle) {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    const uint8_t *peer_addr_or_null = NULL;
    if (term_is_atom(to_term)) {
        if (!globalcontext_is_term_equal_to_atom_string(ctx->global, to_term, broadcast_atom)) {
            return make_error_tuple(ctx, globalcontext_make_atom(ctx->global, not_supported_atom));
        }
        peer_addr_or_null = NULL; // broadcast
    } else if (term_is_binary(to_term)) {
        if (term_binary_size(to_term) != ESP_NOW_ETH_ALEN) {
            return make_error_tuple(ctx, BADARG_ATOM);
        }
        peer_addr_or_null = (const uint8_t *) term_binary_data(to_term);
    } else {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    const uint8_t *data = (const uint8_t *) term_binary_data(data_bin);
    size_t len = (size_t) term_binary_size(data_bin);

    esp_err_t err = avm_espnow_send(handle, peer_addr_or_null, data, len);
    if (err != ESP_OK) {
        return make_error_tuple(ctx, term_from_int(err));
    }

    return OK_ATOM;
}

static term nif_recv(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle_bin = argv[0];
    VALIDATE_VALUE(handle_bin, term_is_binary);

    avm_espnow_handle_t *handle = (avm_espnow_handle_t *) binary_to_ptr(handle_bin);
    if (!handle) {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    avm_espnow_rx_t *rx = NULL;
    esp_err_t err = avm_espnow_recv(handle, &rx);
    if (err == ESP_ERR_TIMEOUT) {
        return globalcontext_make_atom(ctx->global, none_atom);
    }
    if (err != ESP_OK || !rx) {
        return make_error_tuple(ctx, term_from_int(err));
    }

    // Heuristic heap size for tuple + two small binaries.
    size_t approx_words = 32 + ((rx->len + ESP_NOW_ETH_ALEN + 3) / 4);
    if (UNLIKELY(memory_ensure_free(ctx, (unsigned int) approx_words) != MEMORY_GC_OK)) {
        avm_espnow_rx_free(rx);
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }

    term from_bin = term_from_literal_binary(rx->src_addr, ESP_NOW_ETH_ALEN, &ctx->heap, ctx->global);
    term data_bin = term_from_literal_binary(rx->data, rx->len, &ctx->heap, ctx->global);

    avm_espnow_rx_free(rx);

    term ok_tuple = term_alloc_tuple(3, &ctx->heap);
    term_put_tuple_element(ok_tuple, 0, OK_ATOM);
    term_put_tuple_element(ok_tuple, 1, from_bin);
    term_put_tuple_element(ok_tuple, 2, data_bin);
    return ok_tuple;
}

static term nif_poll(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle_bin = argv[0];
    VALIDATE_VALUE(handle_bin, term_is_binary);

    avm_espnow_handle_t *handle = (avm_espnow_handle_t *) binary_to_ptr(handle_bin);
    if (!handle) {
        return make_error_tuple(ctx, BADARG_ATOM);
    }

    avm_espnow_rx_t *rx = NULL;
    avm_espnow_tx_t *tx = NULL;
    esp_err_t err = avm_espnow_poll(handle, &rx, &tx);
    if (err == ESP_ERR_TIMEOUT) {
        return globalcontext_make_atom(ctx->global, none_atom);
    }
    if (err != ESP_OK) {
        return make_error_tuple(ctx, term_from_int(err));
    }

    if (rx) {
        size_t approx_words = 32 + ((rx->len + ESP_NOW_ETH_ALEN + 3) / 4);
        if (UNLIKELY(memory_ensure_free(ctx, (unsigned int) approx_words) != MEMORY_GC_OK)) {
            avm_espnow_rx_free(rx);
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }

        term type_atom = globalcontext_make_atom(ctx->global, rx_atom);
        term from_bin = term_from_literal_binary(rx->src_addr, ESP_NOW_ETH_ALEN, &ctx->heap, ctx->global);
        term data_bin = term_from_literal_binary(rx->data, rx->len, &ctx->heap, ctx->global);
        avm_espnow_rx_free(rx);

        term t = term_alloc_tuple(3, &ctx->heap);
        term_put_tuple_element(t, 0, type_atom);
        term_put_tuple_element(t, 1, from_bin);
        term_put_tuple_element(t, 2, data_bin);
        return t;
    }

    if (tx) {
        if (UNLIKELY(memory_ensure_free(ctx, 16) != MEMORY_GC_OK)) {
            avm_espnow_tx_free(tx);
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }

        term type_atom = globalcontext_make_atom(ctx->global, tx_atom);
        term to_term;
        if (tx->is_broadcast) {
            to_term = globalcontext_make_atom(ctx->global, broadcast_atom);
        } else {
            to_term = term_from_literal_binary(tx->dst_addr, ESP_NOW_ETH_ALEN, &ctx->heap, ctx->global);
        }
        term status_term = term_from_int(tx->status);
        avm_espnow_tx_free(tx);

        term t = term_alloc_tuple(3, &ctx->heap);
        term_put_tuple_element(t, 0, type_atom);
        term_put_tuple_element(t, 1, to_term);
        term_put_tuple_element(t, 2, status_term);
        return t;
    }

    return globalcontext_make_atom(ctx->global, none_atom);
}

static const struct Nif init_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_init
};
static const struct Nif deinit_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_deinit
};
static const struct Nif add_peer_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_add_peer
};
static const struct Nif send_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_send
};
static const struct Nif recv_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_recv
};
static const struct Nif poll_nif = {
    .base.type = NIFFunctionType,
    .nif_ptr = nif_poll
};

void atomvm_espnow_init(GlobalContext *global)
{
    UNUSED(global);
}

void atomvm_espnow_destroy(GlobalContext *global)
{
    UNUSED(global);
}

const struct Nif *atomvm_espnow_get_nif(const char *nifname)
{
    if (strcmp("espnow:nif_init/1", nifname) == 0) {
        return &init_nif;
    }
    if (strcmp("espnow:nif_deinit/1", nifname) == 0) {
        return &deinit_nif;
    }
    if (strcmp("espnow:nif_add_peer/3", nifname) == 0) {
        return &add_peer_nif;
    }
    if (strcmp("espnow:nif_send/3", nifname) == 0) {
        return &send_nif;
    }
    if (strcmp("espnow:nif_recv/1", nifname) == 0) {
        return &recv_nif;
    }
    if (strcmp("espnow:nif_poll/1", nifname) == 0) {
        return &poll_nif;
    }
    return NULL;
}

#include <sdkconfig.h>
#ifdef CONFIG_AVM_ESPNOW_ENABLE
REGISTER_NIF_COLLECTION(atomvm_espnow, atomvm_espnow_init, atomvm_espnow_destroy, atomvm_espnow_get_nif)
#endif
