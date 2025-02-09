/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include "sdkconfig.h"
#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BLE_MESH_DEBUG_FRIEND)

#include "mesh_buf.h"
#include "mesh_util.h"
#include "mesh_main.h"
#include "mesh_trace.h"

#include "mesh.h"
#include "crypto.h"
#include "adv.h"
#include "net.h"
#include "transport.h"
#include "access.h"
#include "foundation.h"
#include "friend.h"

#ifdef CONFIG_BLE_MESH_FRIEND

#define FRIEND_BUF_SIZE     (BLE_MESH_ADV_DATA_SIZE - BLE_MESH_NET_HDR_LEN)

/* We reserve one extra buffer for each friendship, since we need to be able
 * to resend the last sent PDU, which sits separately outside of the queue.
 */
#define FRIEND_BUF_COUNT    ((CONFIG_BLE_MESH_FRIEND_QUEUE_SIZE + 1) * \
                              CONFIG_BLE_MESH_FRIEND_LPN_COUNT)

#define FRIEND_ADV(buf)     CONTAINER_OF(BLE_MESH_ADV(buf), struct friend_adv, adv)

/* PDUs from Friend to the LPN should only be transmitted once with the
 * smallest possible interval (20ms).
 */
#define FRIEND_XMIT         BLE_MESH_TRANSMIT(0, 20)

struct friend_pdu_info {
    u16_t  src;
    u16_t  dst;

    u8_t   seq[3];

    u8_t   ttl: 7,
           ctl: 1;

    u32_t  iv_index;
};

NET_BUF_POOL_DEFINE(friend_buf_pool, FRIEND_BUF_COUNT,
                    BLE_MESH_ADV_DATA_SIZE, BLE_MESH_ADV_USER_DATA_SIZE, NULL);

static struct friend_adv {
    struct bt_mesh_adv adv;
    u64_t seq_auth;
} adv_pool[FRIEND_BUF_COUNT];

static struct bt_mesh_adv *adv_alloc(int id)
{
    return &adv_pool[id].adv;
}

static void discard_buffer(void)
{
    struct bt_mesh_friend *frnd = &bt_mesh.frnd[0];
    struct net_buf *buf;
    int i;

    /* Find the Friend context with the most queued buffers */
    for (i = 1; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        if (bt_mesh.frnd[i].queue_size > frnd->queue_size) {
            frnd = &bt_mesh.frnd[i];
        }
    }

    buf = net_buf_slist_get(&frnd->queue);
    __ASSERT_NO_MSG(buf != NULL);
    BT_WARN("%s, Discarding buffer %p for LPN 0x%04x", __func__, buf, frnd->lpn);
    net_buf_unref(buf);
}

static struct net_buf *friend_buf_alloc(u16_t src)
{
    struct net_buf *buf;

    BT_DBG("src 0x%04x", src);

    do {
        buf = bt_mesh_adv_create_from_pool(&friend_buf_pool, adv_alloc,
                                           BLE_MESH_ADV_DATA,
                                           BLE_MESH_TRANSMIT_COUNT(FRIEND_XMIT),
                                           BLE_MESH_TRANSMIT_INT(FRIEND_XMIT),
                                           K_NO_WAIT);
        if (!buf) {
            discard_buffer();
        }
    } while (!buf);

    BLE_MESH_ADV(buf)->addr = src;
    FRIEND_ADV(buf)->seq_auth = TRANS_SEQ_AUTH_NVAL;

    BT_DBG("allocated buf %p", buf);

    return buf;
}

static bool is_lpn_unicast(struct bt_mesh_friend *frnd, u16_t addr)
{
    if (frnd->lpn == BLE_MESH_ADDR_UNASSIGNED) {
        return false;
    }

    return (addr >= frnd->lpn && addr < (frnd->lpn + frnd->num_elem));
}

struct bt_mesh_friend *bt_mesh_friend_find(u16_t net_idx, u16_t lpn_addr,
        bool valid, bool established)
{
    int i;

    BT_DBG("net_idx 0x%04x lpn_addr 0x%04x", net_idx, lpn_addr);

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];

        if (valid && !frnd->valid) {
            continue;
        }

        if (established && !frnd->established) {
            continue;
        }

        if (net_idx != BLE_MESH_KEY_ANY && frnd->net_idx != net_idx) {
            continue;
        }

        if (is_lpn_unicast(frnd, lpn_addr)) {
            return frnd;
        }
    }

    return NULL;
}

/* Intentionally start a little bit late into the ReceiveWindow when
 * it's large enough. This may improve reliability with some platforms,
 * like the PTS, where the receiver might not have sufficiently compensated
 * for internal latencies required to start scanning.
 */
static s32_t recv_delay(struct bt_mesh_friend *frnd)
{
#if CONFIG_BLE_MESH_FRIEND_RECV_WIN > 50
    return (s32_t)frnd->recv_delay + (CONFIG_BLE_MESH_FRIEND_RECV_WIN / 5);
#else
    return frnd->recv_delay;
#endif
}

static void friend_clear(struct bt_mesh_friend *frnd)
{
    int i;

    BT_DBG("LPN 0x%04x", frnd->lpn);

    k_delayed_work_cancel(&frnd->timer);

    friend_cred_del(frnd->net_idx, frnd->lpn);

    if (frnd->last) {
        /* Cancel the sending if necessary */
        if (frnd->pending_buf) {
            BLE_MESH_ADV(frnd->last)->busy = 0;
        }

        net_buf_unref(frnd->last);
        frnd->last = NULL;
    }

    while (!sys_slist_is_empty(&frnd->queue)) {
        net_buf_unref(net_buf_slist_get(&frnd->queue));
    }

    for (i = 0; i < ARRAY_SIZE(frnd->seg); i++) {
        struct bt_mesh_friend_seg *seg = &frnd->seg[i];

        while (!sys_slist_is_empty(&seg->queue)) {
            net_buf_unref(net_buf_slist_get(&seg->queue));
        }
    }

    frnd->valid = 0;
    frnd->established = 0;
    frnd->pending_buf = 0;
    frnd->fsn = 0;
    frnd->queue_size = 0;
    frnd->pending_req = 0;
    memset(frnd->sub_list, 0, sizeof(frnd->sub_list));
}

void bt_mesh_friend_clear_net_idx(u16_t net_idx)
{
    int i;

    BT_DBG("net_idx 0x%04x", net_idx);

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];

        if (frnd->net_idx == BLE_MESH_KEY_UNUSED) {
            continue;
        }

        if (net_idx == BLE_MESH_KEY_ANY || frnd->net_idx == net_idx) {
            friend_clear(frnd);
        }
    }
}

void bt_mesh_friend_sec_update(u16_t net_idx)
{
    int i;

    BT_DBG("net_idx 0x%04x", net_idx);

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];

        if (frnd->net_idx == BLE_MESH_KEY_UNUSED) {
            continue;
        }

        if (net_idx == BLE_MESH_KEY_ANY || frnd->net_idx == net_idx) {
            frnd->sec_update = 1;
        }
    }
}

int bt_mesh_friend_clear(struct bt_mesh_net_rx *rx, struct net_buf_simple *buf)
{
    struct bt_mesh_ctl_friend_clear *msg = (void *)buf->data;
    struct bt_mesh_friend *frnd;
    u16_t lpn_addr, lpn_counter;
    struct bt_mesh_net_tx tx = {
        .sub  = rx->sub,
        .ctx  = &rx->ctx,
        .src  = bt_mesh_primary_addr(),
        .xmit = bt_mesh_net_transmit_get(),
    };
    struct bt_mesh_ctl_friend_clear_confirm cfm;

    if (buf->len < sizeof(*msg)) {
        BT_WARN("%s, Too short Friend Clear", __func__);
        return -EINVAL;
    }

    lpn_addr = sys_be16_to_cpu(msg->lpn_addr);
    lpn_counter = sys_be16_to_cpu(msg->lpn_counter);

    BT_DBG("LPN addr 0x%04x counter 0x%04x", lpn_addr, lpn_counter);

    frnd = bt_mesh_friend_find(rx->sub->net_idx, lpn_addr, false, false);
    if (!frnd) {
        BT_WARN("%s, No matching LPN addr 0x%04x", __func__, lpn_addr);
        return 0;
    }

    /* A Friend Clear message is considered valid if the result of the
     * subtraction of the value of the LPNCounter field of the Friend
     * Request message (the one that initiated the friendship) from the
     * value of the LPNCounter field of the Friend Clear message, modulo
     * 65536, is in the range 0 to 255 inclusive.
     */
    if (lpn_counter - frnd->lpn_counter > 255) {
        BT_WARN("%s, LPN Counter out of range (old %u new %u)",
                __func__, frnd->lpn_counter, lpn_counter);
        return 0;
    }

    tx.ctx->send_ttl = BLE_MESH_TTL_MAX;

    cfm.lpn_addr    = msg->lpn_addr;
    cfm.lpn_counter = msg->lpn_counter;

    bt_mesh_ctl_send(&tx, TRANS_CTL_OP_FRIEND_CLEAR_CFM, &cfm,
                     sizeof(cfm), NULL, NULL, NULL);

    friend_clear(frnd);

    return 0;
}

static void friend_sub_add(struct bt_mesh_friend *frnd, u16_t addr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(frnd->sub_list); i++) {
        if (frnd->sub_list[i] == BLE_MESH_ADDR_UNASSIGNED) {
            frnd->sub_list[i] = addr;
            return;
        }
    }

    BT_WARN("%s, No space in friend subscription list", __func__);
}

static void friend_sub_rem(struct bt_mesh_friend *frnd, u16_t addr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(frnd->sub_list); i++) {
        if (frnd->sub_list[i] == addr) {
            frnd->sub_list[i] = BLE_MESH_ADDR_UNASSIGNED;
            return;
        }
    }
}

static struct net_buf *create_friend_pdu(struct bt_mesh_friend *frnd,
        struct friend_pdu_info *info,
        struct net_buf_simple *sdu)
{
    struct bt_mesh_subnet *sub;
    const u8_t *enc, *priv;
    struct net_buf *buf;
    u8_t nid;

    sub = bt_mesh_subnet_get(frnd->net_idx);
    __ASSERT_NO_MSG(sub != NULL);

    buf = friend_buf_alloc(info->src);

    /* Friend Offer needs master security credentials */
    if (info->ctl && TRANS_CTL_OP(sdu->data) == TRANS_CTL_OP_FRIEND_OFFER) {
        enc = sub->keys[sub->kr_flag].enc;
        priv = sub->keys[sub->kr_flag].privacy;
        nid = sub->keys[sub->kr_flag].nid;
    } else {
        if (friend_cred_get(sub, frnd->lpn, &nid, &enc, &priv)) {
            BT_ERR("%s, friend_cred_get failed", __func__);
            goto failed;
        }
    }

    net_buf_add_u8(buf, (nid | (info->iv_index & 1) << 7));

    if (info->ctl) {
        net_buf_add_u8(buf, info->ttl | 0x80);
    } else {
        net_buf_add_u8(buf, info->ttl);
    }

    net_buf_add_mem(buf, info->seq, sizeof(info->seq));

    net_buf_add_be16(buf, info->src);
    net_buf_add_be16(buf, info->dst);

    net_buf_add_mem(buf, sdu->data, sdu->len);

    /* We re-encrypt and obfuscate using the received IVI rather than
     * the normal TX IVI (which may be different) since the transport
     * layer nonce includes the IVI.
     */
    if (bt_mesh_net_encrypt(enc, &buf->b, info->iv_index, false)) {
        BT_ERR("%s, Re-encrypting failed", __func__);
        goto failed;
    }

    if (bt_mesh_net_obfuscate(buf->data, info->iv_index, priv)) {
        BT_ERR("%s, Re-obfuscating failed", __func__);
        goto failed;
    }

    return buf;

failed:
    net_buf_unref(buf);
    return NULL;
}

static struct net_buf *encode_friend_ctl(struct bt_mesh_friend *frnd,
        u8_t ctl_op,
        struct net_buf_simple *sdu)
{
    struct friend_pdu_info info;
    u32_t seq;

    BT_DBG("LPN 0x%04x", frnd->lpn);

    net_buf_simple_push_u8(sdu, TRANS_CTL_HDR(ctl_op, 0));

    info.src = bt_mesh_primary_addr();
    info.dst = frnd->lpn;

    info.ctl = 1;
    info.ttl = 0;

    seq = bt_mesh_next_seq();
    info.seq[0] = seq >> 16;
    info.seq[1] = seq >> 8;
    info.seq[2] = seq;

    info.iv_index = BLE_MESH_NET_IVI_TX;

    return create_friend_pdu(frnd, &info, sdu);
}

static struct net_buf *encode_update(struct bt_mesh_friend *frnd, u8_t md)
{
    struct bt_mesh_ctl_friend_update *upd;
    struct net_buf_simple *sdu = NET_BUF_SIMPLE(1 + sizeof(*upd));
    struct bt_mesh_subnet *sub = bt_mesh_subnet_get(frnd->net_idx);

    __ASSERT_NO_MSG(sub != NULL);

    BT_DBG("lpn 0x%04x md 0x%02x", frnd->lpn, md);

    net_buf_simple_init(sdu, 1);

    upd = net_buf_simple_add(sdu, sizeof(*upd));
    upd->flags = bt_mesh_net_flags(sub);
    upd->iv_index = sys_cpu_to_be32(bt_mesh.iv_index);
    upd->md = md;

    return encode_friend_ctl(frnd, TRANS_CTL_OP_FRIEND_UPDATE, sdu);
}

static void enqueue_sub_cfm(struct bt_mesh_friend *frnd, u8_t xact)
{
    struct bt_mesh_ctl_friend_sub_confirm *cfm;
    struct net_buf_simple *sdu = NET_BUF_SIMPLE(1 + sizeof(*cfm));
    struct net_buf *buf;

    BT_DBG("lpn 0x%04x xact 0x%02x", frnd->lpn, xact);

    net_buf_simple_init(sdu, 1);

    cfm = net_buf_simple_add(sdu, sizeof(*cfm));
    cfm->xact = xact;

    buf = encode_friend_ctl(frnd, TRANS_CTL_OP_FRIEND_SUB_CFM, sdu);
    if (!buf) {
        BT_ERR("%s, Unable to encode Subscription List Confirmation", __func__);
        return;
    }

    if (frnd->last) {
        BT_DBG("Discarding last PDU");
        net_buf_unref(frnd->last);
    }

    frnd->last = buf;
    frnd->send_last = 1;
}

static void friend_recv_delay(struct bt_mesh_friend *frnd)
{
    frnd->pending_req = 1;
    k_delayed_work_submit(&frnd->timer, recv_delay(frnd));
    BT_DBG("Waiting RecvDelay of %d ms", recv_delay(frnd));
}

int bt_mesh_friend_sub_add(struct bt_mesh_net_rx *rx,
                           struct net_buf_simple *buf)
{
    struct bt_mesh_friend *frnd;
    u8_t xact;

    if (buf->len < BLE_MESH_FRIEND_SUB_MIN_LEN) {
        BT_WARN("%s, Too short Friend Subscription Add", __func__);
        return -EINVAL;
    }

    frnd = bt_mesh_friend_find(rx->sub->net_idx, rx->ctx.addr, true, true);
    if (!frnd) {
        BT_WARN("%s, No matching LPN addr 0x%04x", __func__, rx->ctx.addr);
        return 0;
    }

    if (frnd->pending_buf) {
        BT_WARN("%s, Previous buffer not yet sent!", __func__);
        return 0;
    }

    friend_recv_delay(frnd);

    xact = net_buf_simple_pull_u8(buf);

    while (buf->len >= 2) {
        friend_sub_add(frnd, net_buf_simple_pull_be16(buf));
    }

    enqueue_sub_cfm(frnd, xact);

    return 0;
}

int bt_mesh_friend_sub_rem(struct bt_mesh_net_rx *rx,
                           struct net_buf_simple *buf)
{
    struct bt_mesh_friend *frnd;
    u8_t xact;

    if (buf->len < BLE_MESH_FRIEND_SUB_MIN_LEN) {
        BT_WARN("%s, Too short Friend Subscription Remove", __func__);
        return -EINVAL;
    }

    frnd = bt_mesh_friend_find(rx->sub->net_idx, rx->ctx.addr, true, true);
    if (!frnd) {
        BT_WARN("%s, No matching LPN addr 0x%04x", __func__, rx->ctx.addr);
        return 0;
    }

    if (frnd->pending_buf) {
        BT_WARN("%s, Previous buffer not yet sent!", __func__);
        return 0;
    }

    friend_recv_delay(frnd);

    xact = net_buf_simple_pull_u8(buf);

    while (buf->len >= 2) {
        friend_sub_rem(frnd, net_buf_simple_pull_be16(buf));
    }

    enqueue_sub_cfm(frnd, xact);

    return 0;
}

static void enqueue_buf(struct bt_mesh_friend *frnd, struct net_buf *buf)
{
    net_buf_slist_put(&frnd->queue, buf);
    frnd->queue_size++;
}

static void enqueue_update(struct bt_mesh_friend *frnd, u8_t md)
{
    struct net_buf *buf;

    buf = encode_update(frnd, md);
    if (!buf) {
        BT_ERR("%s, Unable to encode Friend Update", __func__);
        return;
    }

    frnd->sec_update = 0;
    enqueue_buf(frnd, buf);
}

int bt_mesh_friend_poll(struct bt_mesh_net_rx *rx, struct net_buf_simple *buf)
{
    struct bt_mesh_ctl_friend_poll *msg = (void *)buf->data;
    struct bt_mesh_friend *frnd;

    if (buf->len < sizeof(*msg)) {
        BT_WARN("%s, Too short Friend Poll", __func__);
        return -EINVAL;
    }

    frnd = bt_mesh_friend_find(rx->sub->net_idx, rx->ctx.addr, true, false);
    if (!frnd) {
        BT_WARN("%s, No matching LPN addr 0x%04x", __func__, rx->ctx.addr);
        return 0;
    }

    if (msg->fsn & ~1) {
        BT_WARN("%s, Prohibited (non-zero) padding bits", __func__);
        return -EINVAL;
    }

    if (frnd->pending_buf) {
        BT_WARN("%s, Previous buffer not yet sent!", __func__);
        return 0;
    }

    BT_DBG("msg->fsn %u frnd->fsn %u", (msg->fsn & 1), frnd->fsn);

    friend_recv_delay(frnd);

    if (!frnd->established) {
        BT_DBG("Friendship established with 0x%04x", frnd->lpn);
        frnd->established = 1;
    }

    if (msg->fsn == frnd->fsn && frnd->last) {
        BT_DBG("Re-sending last PDU");
        frnd->send_last = 1;
    } else {
        if (frnd->last) {
            net_buf_unref(frnd->last);
            frnd->last = NULL;
        }

        frnd->fsn = msg->fsn;

        if (sys_slist_is_empty(&frnd->queue)) {
            enqueue_update(frnd, 0);
            BT_DBG("Enqueued Friend Update to empty queue");
        }
    }

    return 0;
}

static struct bt_mesh_friend *find_clear(u16_t prev_friend)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];

        if (frnd->clear.frnd == prev_friend) {
            return frnd;
        }
    }

    return NULL;
}

static void friend_clear_sent(int err, void *user_data)
{
    struct bt_mesh_friend *frnd = user_data;

    k_delayed_work_submit(&frnd->clear.timer,
                          K_SECONDS(frnd->clear.repeat_sec));
    frnd->clear.repeat_sec *= 2;
}

static const struct bt_mesh_send_cb clear_sent_cb = {
    .end = friend_clear_sent,
};

static void send_friend_clear(struct bt_mesh_friend *frnd)
{
    struct bt_mesh_msg_ctx ctx = {
        .net_idx  = frnd->net_idx,
        .app_idx  = BLE_MESH_KEY_UNUSED,
        .addr     = frnd->clear.frnd,
        .send_ttl = BLE_MESH_TTL_MAX,
    };
    struct bt_mesh_net_tx tx = {
        .sub  = &bt_mesh.sub[0],
        .ctx  = &ctx,
        .src  = bt_mesh_primary_addr(),
        .xmit = bt_mesh_net_transmit_get(),
    };
    struct bt_mesh_ctl_friend_clear req = {
        .lpn_addr    = sys_cpu_to_be16(frnd->lpn),
        .lpn_counter = sys_cpu_to_be16(frnd->lpn_counter),
    };

    BT_DBG("%s", __func__);

    bt_mesh_ctl_send(&tx, TRANS_CTL_OP_FRIEND_CLEAR, &req,
                     sizeof(req), NULL, &clear_sent_cb, frnd);
}

static void clear_timeout(struct k_work *work)
{
    struct bt_mesh_friend *frnd = CONTAINER_OF(work, struct bt_mesh_friend,
                                  clear.timer.work);
    u32_t duration;

    BT_DBG("LPN 0x%04x (old) Friend 0x%04x", frnd->lpn, frnd->clear.frnd);

    duration = k_uptime_get_32() - frnd->clear.start;
    if (duration > 2 * frnd->poll_to) {
        BT_DBG("Clear Procedure timer expired");
        frnd->clear.frnd = BLE_MESH_ADDR_UNASSIGNED;
        return;
    }

    send_friend_clear(frnd);
}

static void clear_procedure_start(struct bt_mesh_friend *frnd)
{
    BT_DBG("LPN 0x%04x (old) Friend 0x%04x", frnd->lpn, frnd->clear.frnd);

    frnd->clear.start = k_uptime_get_32() + (2 * frnd->poll_to);
    frnd->clear.repeat_sec = 1;

    send_friend_clear(frnd);
}

int bt_mesh_friend_clear_cfm(struct bt_mesh_net_rx *rx,
                             struct net_buf_simple *buf)
{
    struct bt_mesh_ctl_friend_clear_confirm *msg = (void *)buf->data;
    struct bt_mesh_friend *frnd;
    u16_t lpn_addr, lpn_counter;

    BT_DBG("%s", __func__);

    if (buf->len < sizeof(*msg)) {
        BT_WARN("%s, Too short Friend Clear Confirm", __func__);
        return -EINVAL;
    }

    frnd = find_clear(rx->ctx.addr);
    if (!frnd) {
        BT_WARN("%s, No pending clear procedure for 0x%02x", __func__, rx->ctx.addr);
        return 0;
    }

    lpn_addr = sys_be16_to_cpu(msg->lpn_addr);
    if (lpn_addr != frnd->lpn) {
        BT_WARN("%s, LPN address mismatch (0x%04x != 0x%04x)",
                __func__, lpn_addr, frnd->lpn);
        return 0;
    }

    lpn_counter = sys_be16_to_cpu(msg->lpn_counter);
    if (lpn_counter != frnd->lpn_counter) {
        BT_WARN("%s, LPN counter mismatch (0x%04x != 0x%04x)",
                __func__, lpn_counter, frnd->lpn_counter);
        return 0;
    }

    k_delayed_work_cancel(&frnd->clear.timer);
    frnd->clear.frnd = BLE_MESH_ADDR_UNASSIGNED;

    return 0;
}

static void enqueue_offer(struct bt_mesh_friend *frnd, s8_t rssi)
{
    struct bt_mesh_ctl_friend_offer *off;
    struct net_buf_simple *sdu = NET_BUF_SIMPLE(1 + sizeof(*off));
    struct net_buf *buf;

    BT_DBG("%s", __func__);

    net_buf_simple_init(sdu, 1);

    off = net_buf_simple_add(sdu, sizeof(*off));

    off->recv_win = CONFIG_BLE_MESH_FRIEND_RECV_WIN,
         off->queue_size = CONFIG_BLE_MESH_FRIEND_QUEUE_SIZE,
              off->sub_list_size = ARRAY_SIZE(frnd->sub_list),
                   off->rssi = rssi,
                        off->frnd_counter = sys_cpu_to_be16(frnd->counter);

    buf = encode_friend_ctl(frnd, TRANS_CTL_OP_FRIEND_OFFER, sdu);
    if (!buf) {
        BT_ERR("%s, Unable to encode Friend Offer", __func__);
        return;
    }

    frnd->counter++;

    if (frnd->last) {
        net_buf_unref(frnd->last);
    }

    frnd->last = buf;
    frnd->send_last = 1;
}

#define RECV_WIN                  CONFIG_BLE_MESH_FRIEND_RECV_WIN
#define RSSI_FACT(crit)           (((crit) >> 5) & (u8_t)BIT_MASK(2))
#define RECV_WIN_FACT(crit)       (((crit) >> 3) & (u8_t)BIT_MASK(2))
#define MIN_QUEUE_SIZE_LOG(crit)  ((crit) & (u8_t)BIT_MASK(3))
#define MIN_QUEUE_SIZE(crit)      ((u32_t)BIT(MIN_QUEUE_SIZE_LOG(crit)))

static s32_t offer_delay(struct bt_mesh_friend *frnd, s8_t rssi, u8_t crit)
{
    /* Scaling factors. The actual values are 1, 1.5, 2 & 2.5, but we
     * want to avoid floating-point arithmetic.
     */
    static const u8_t fact[] = { 10, 15, 20, 25 };
    s32_t delay;

    BT_DBG("ReceiveWindowFactor %u ReceiveWindow %u RSSIFactor %u RSSI %d",
           fact[RECV_WIN_FACT(crit)], RECV_WIN,
           fact[RSSI_FACT(crit)], rssi);

    /* Delay = ReceiveWindowFactor * ReceiveWindow - RSSIFactor * RSSI */
    delay = (s32_t)fact[RECV_WIN_FACT(crit)] * RECV_WIN;
    delay -= (s32_t)fact[RSSI_FACT(crit)] * rssi;
    delay /= 10;

    BT_DBG("Local Delay calculated as %d ms", delay);

    if (delay < 100) {
        return K_MSEC(100);
    }

    return K_MSEC(delay);
}

int bt_mesh_friend_req(struct bt_mesh_net_rx *rx, struct net_buf_simple *buf)
{
    struct bt_mesh_ctl_friend_req *msg = (void *)buf->data;
    struct bt_mesh_friend *frnd = NULL;
    u32_t poll_to;
    int i;

    if (buf->len < sizeof(*msg)) {
        BT_WARN("%s, Too short Friend Request", __func__);
        return -EINVAL;
    }

    if (msg->recv_delay <= 0x09) {
        BT_WARN("%s, Prohibited ReceiveDelay (0x%02x)", __func__, msg->recv_delay);
        return -EINVAL;
    }

    poll_to = (((u32_t)msg->poll_to[0] << 16) |
               ((u32_t)msg->poll_to[1] << 8) |
               ((u32_t)msg->poll_to[2]));

    if (poll_to <= 0x000009 || poll_to >= 0x34bc00) {
        BT_WARN("%s, Prohibited PollTimeout (0x%06x)", __func__, poll_to);
        return -EINVAL;
    }

    if (msg->num_elem == 0x00) {
        BT_WARN("%s, Prohibited NumElements value (0x00)", __func__);
        return -EINVAL;
    }

    if (!BLE_MESH_ADDR_IS_UNICAST(rx->ctx.addr + msg->num_elem - 1)) {
        BT_WARN("%s, LPN elements stretch outside of unicast range", __func__);
        return -EINVAL;
    }

    if (!MIN_QUEUE_SIZE_LOG(msg->criteria)) {
        BT_WARN("%s, Prohibited Minimum Queue Size in Friend Request", __func__);
        return -EINVAL;
    }

    if (CONFIG_BLE_MESH_FRIEND_QUEUE_SIZE < MIN_QUEUE_SIZE(msg->criteria)) {
        BT_WARN("%s, We have a too small Friend Queue size (%u < %u)",
                __func__, CONFIG_BLE_MESH_FRIEND_QUEUE_SIZE,
                MIN_QUEUE_SIZE(msg->criteria));
        return 0;
    }

    frnd = bt_mesh_friend_find(rx->sub->net_idx, rx->ctx.addr, true, false);
    if (frnd) {
        BT_WARN("%s, Existing LPN re-requesting Friendship", __func__);
        friend_clear(frnd);
        goto init_friend;
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        if (!bt_mesh.frnd[i].valid) {
            frnd = &bt_mesh.frnd[i];
            frnd->valid = 1;
            break;
        }
    }

    if (!frnd) {
        BT_WARN("%s, No free Friend contexts for new LPN", __func__);
        return -ENOMEM;
    }

init_friend:
    frnd->lpn = rx->ctx.addr;
    frnd->num_elem = msg->num_elem;
    frnd->net_idx = rx->sub->net_idx;
    frnd->recv_delay = msg->recv_delay;
    frnd->poll_to = poll_to * 100;
    frnd->lpn_counter = sys_be16_to_cpu(msg->lpn_counter);
    frnd->clear.frnd = sys_be16_to_cpu(msg->prev_addr);

    BT_DBG("LPN 0x%04x rssi %d recv_delay %u poll_to %ums",
           frnd->lpn, rx->rssi, frnd->recv_delay, frnd->poll_to);

    if (BLE_MESH_ADDR_IS_UNICAST(frnd->clear.frnd) &&
            !bt_mesh_elem_find(frnd->clear.frnd)) {
        clear_procedure_start(frnd);
    }

    k_delayed_work_submit(&frnd->timer,
                          offer_delay(frnd, rx->rssi, msg->criteria));

    friend_cred_create(rx->sub, frnd->lpn, frnd->lpn_counter,
                       frnd->counter);

    enqueue_offer(frnd, rx->rssi);

    return 0;
}

static struct bt_mesh_friend_seg *get_seg(struct bt_mesh_friend *frnd,
        u16_t src, u64_t *seq_auth)
{
    struct bt_mesh_friend_seg *unassigned = NULL;
    int i;

    for (i = 0; i < ARRAY_SIZE(frnd->seg); i++) {
        struct bt_mesh_friend_seg *seg = &frnd->seg[i];
        struct net_buf *buf = (void *)sys_slist_peek_head(&seg->queue);

        if (buf && BLE_MESH_ADV(buf)->addr == src &&
                FRIEND_ADV(buf)->seq_auth == *seq_auth) {
            return seg;
        }

        if (!unassigned && !buf) {
            unassigned = seg;
        }
    }

    return unassigned;
}

static void enqueue_friend_pdu(struct bt_mesh_friend *frnd,
                               enum bt_mesh_friend_pdu_type type,
                               struct net_buf *buf)
{
    struct bt_mesh_friend_seg *seg;
    struct friend_adv *adv;

    BT_DBG("type %u", type);

    if (type == BLE_MESH_FRIEND_PDU_SINGLE) {
        if (frnd->sec_update) {
            enqueue_update(frnd, 1);
        }

        enqueue_buf(frnd, buf);
        return;
    }

    adv = FRIEND_ADV(buf);
    seg = get_seg(frnd, BLE_MESH_ADV(buf)->addr, &adv->seq_auth);
    if (!seg) {
        BT_ERR("%s, No free friend segment RX contexts for 0x%04x",
                __func__, BLE_MESH_ADV(buf)->addr);
        net_buf_unref(buf);
        return;
    }

    net_buf_slist_put(&seg->queue, buf);

    if (type == BLE_MESH_FRIEND_PDU_COMPLETE) {
        if (frnd->sec_update) {
            enqueue_update(frnd, 1);
        }

        /* Only acks should have a valid SeqAuth in the Friend queue
         * (otherwise we can't easily detect them there), so clear
         * the SeqAuth information from the segments before merging.
         */
        SYS_SLIST_FOR_EACH_CONTAINER(&seg->queue, buf, node) {
            FRIEND_ADV(buf)->seq_auth = TRANS_SEQ_AUTH_NVAL;
            frnd->queue_size++;
        }

        sys_slist_merge_slist(&frnd->queue, &seg->queue);
    }
}

static void buf_send_start(u16_t duration, int err, void *user_data)
{
    struct bt_mesh_friend *frnd = user_data;

    BT_DBG("err %d", err);

    frnd->pending_buf = 0;

    /* Friend Offer doesn't follow the re-sending semantics */
    if (!frnd->established) {
        net_buf_unref(frnd->last);
        frnd->last = NULL;
    }
}

static void buf_send_end(int err, void *user_data)
{
    struct bt_mesh_friend *frnd = user_data;

    BT_DBG("err %d", err);

    if (frnd->pending_req) {
        BT_WARN("%s, Another request before previous completed sending", __func__);
        return;
    }

    if (frnd->established) {
        k_delayed_work_submit(&frnd->timer, frnd->poll_to);
        BT_DBG("Waiting %u ms for next poll", frnd->poll_to);
    } else {
        /* Friend offer timeout is 1 second */
        k_delayed_work_submit(&frnd->timer, K_SECONDS(1));
        BT_DBG("Waiting for first poll");
    }
}

static void friend_timeout(struct k_work *work)
{
    struct bt_mesh_friend *frnd = CONTAINER_OF(work, struct bt_mesh_friend,
                                  timer.work);
    static const struct bt_mesh_send_cb buf_sent_cb = {
        .start = buf_send_start,
        .end = buf_send_end,
    };

    __ASSERT_NO_MSG(frnd->pending_buf == 0);

    BT_DBG("lpn 0x%04x send_last %u last %p", frnd->lpn,
           frnd->send_last, frnd->last);

    if (frnd->send_last && frnd->last) {
        BT_DBG("Sending frnd->last %p", frnd->last);
        frnd->send_last = 0;
        goto send_last;
    }

    if (frnd->established && !frnd->pending_req) {
        BT_WARN("%s, Friendship lost with 0x%04x", __func__, frnd->lpn);
        friend_clear(frnd);
        return;
    }

    frnd->last = net_buf_slist_get(&frnd->queue);
    if (!frnd->last) {
        BT_WARN("%s, Friendship is not established with 0x%04x", __func__, frnd->lpn);
        friend_clear(frnd);
        return;
    }

    BT_DBG("Sending buf %p from Friend Queue of LPN 0x%04x",
           frnd->last, frnd->lpn);
    frnd->queue_size--;

send_last:
    frnd->pending_req = 0;
    frnd->pending_buf = 1;
    bt_mesh_adv_send(frnd->last, &buf_sent_cb, frnd);
}

int bt_mesh_friend_init(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];
        int j;

        frnd->net_idx = BLE_MESH_KEY_UNUSED;

        sys_slist_init(&frnd->queue);

        k_delayed_work_init(&frnd->timer, friend_timeout);
        k_delayed_work_init(&frnd->clear.timer, clear_timeout);

        for (j = 0; j < ARRAY_SIZE(frnd->seg); j++) {
            sys_slist_init(&frnd->seg[j].queue);
        }
    }

    return 0;
}

static void friend_purge_old_ack(struct bt_mesh_friend *frnd, u64_t *seq_auth,
                                 u16_t src)
{
    sys_snode_t *cur, *prev = NULL;

    BT_DBG("SeqAuth %llx src 0x%04x", *seq_auth, src);

    for (cur = sys_slist_peek_head(&frnd->queue);
            cur != NULL; prev = cur, cur = sys_slist_peek_next(cur)) {
        struct net_buf *buf = (void *)cur;

        if (BLE_MESH_ADV(buf)->addr == src &&
                FRIEND_ADV(buf)->seq_auth == *seq_auth) {
            BT_DBG("Removing old ack from Friend Queue");

            sys_slist_remove(&frnd->queue, prev, cur);
            frnd->queue_size--;
            /* Make sure old slist entry state doesn't remain */
            buf->frags = NULL;

            net_buf_unref(buf);
            break;
        }
    }
}

static void friend_lpn_enqueue_rx(struct bt_mesh_friend *frnd,
                                  struct bt_mesh_net_rx *rx,
                                  enum bt_mesh_friend_pdu_type type,
                                  u64_t *seq_auth, struct net_buf_simple *sbuf)
{
    struct friend_pdu_info info;
    struct net_buf *buf;

    BT_DBG("LPN 0x%04x queue_size %u", frnd->lpn, frnd->queue_size);

    if (type == BLE_MESH_FRIEND_PDU_SINGLE && seq_auth) {
        friend_purge_old_ack(frnd, seq_auth, rx->ctx.addr);
    }

    info.src = rx->ctx.addr;
    info.dst = rx->dst;

    if (rx->net_if == BLE_MESH_NET_IF_LOCAL) {
        info.ttl = rx->ctx.recv_ttl;
    } else {
        info.ttl = rx->ctx.recv_ttl - 1;
    }

    info.ctl = rx->ctl;

    info.seq[0] = (rx->seq >> 16);
    info.seq[1] = (rx->seq >> 8);
    info.seq[2] = rx->seq;

    info.iv_index = BLE_MESH_NET_IVI_RX(rx);

    buf = create_friend_pdu(frnd, &info, sbuf);
    if (!buf) {
        BT_ERR("%s, Failed to encode Friend buffer", __func__);
        return;
    }

    if (seq_auth) {
        FRIEND_ADV(buf)->seq_auth = *seq_auth;
    }

    enqueue_friend_pdu(frnd, type, buf);

    BT_DBG("Queued message for LPN 0x%04x, queue_size %u",
           frnd->lpn, frnd->queue_size);
}

static void friend_lpn_enqueue_tx(struct bt_mesh_friend *frnd,
                                  struct bt_mesh_net_tx *tx,
                                  enum bt_mesh_friend_pdu_type type,
                                  u64_t *seq_auth, struct net_buf_simple *sbuf)
{
    struct friend_pdu_info info;
    struct net_buf *buf;
    u32_t seq;

    BT_DBG("LPN 0x%04x", frnd->lpn);

    if (type == BLE_MESH_FRIEND_PDU_SINGLE && seq_auth) {
        friend_purge_old_ack(frnd, seq_auth, tx->src);
    }

    info.src = tx->src;
    info.dst = tx->ctx->addr;

    info.ttl = tx->ctx->send_ttl;
    info.ctl = (tx->ctx->app_idx == BLE_MESH_KEY_UNUSED);

    seq = bt_mesh_next_seq();
    info.seq[0] = seq >> 16;
    info.seq[1] = seq >> 8;
    info.seq[2] = seq;

    info.iv_index = BLE_MESH_NET_IVI_TX;

    buf = create_friend_pdu(frnd, &info, sbuf);
    if (!buf) {
        BT_ERR("%s, Failed to encode Friend buffer", __func__);
        return;
    }

    if (seq_auth) {
        FRIEND_ADV(buf)->seq_auth = *seq_auth;
    }

    enqueue_friend_pdu(frnd, type, buf);

    BT_DBG("Queued message for LPN 0x%04x", frnd->lpn);
}

static bool friend_lpn_matches(struct bt_mesh_friend *frnd, u16_t net_idx,
                               u16_t addr)
{
    int i;

    if (!frnd->established) {
        return false;
    }

    if (net_idx != frnd->net_idx) {
        return false;
    }

    if (BLE_MESH_ADDR_IS_UNICAST(addr)) {
        return is_lpn_unicast(frnd, addr);
    }

    for (i = 0; i < ARRAY_SIZE(frnd->sub_list); i++) {
        if (frnd->sub_list[i] == addr) {
            return true;
        }
    }

    return false;
}

bool bt_mesh_friend_match(u16_t net_idx, u16_t addr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];

        if (friend_lpn_matches(frnd, net_idx, addr)) {
            BT_DBG("LPN 0x%04x matched address 0x%04x",
                   frnd->lpn, addr);
            return true;
        }
    }

    BT_DBG("No matching LPN for address 0x%04x", addr);

    return false;
}

void bt_mesh_friend_enqueue_rx(struct bt_mesh_net_rx *rx,
                               enum bt_mesh_friend_pdu_type type,
                               u64_t *seq_auth, struct net_buf_simple *sbuf)
{
    int i;

    if (!rx->friend_match ||
            (rx->ctx.recv_ttl <= 1 && rx->net_if != BLE_MESH_NET_IF_LOCAL) ||
            bt_mesh_friend_get() != BLE_MESH_FRIEND_ENABLED) {
        return;
    }

    BT_DBG("recv_ttl %u net_idx 0x%04x src 0x%04x dst 0x%04x",
           rx->ctx.recv_ttl, rx->sub->net_idx, rx->ctx.addr, rx->dst);

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];

        if (friend_lpn_matches(frnd, rx->sub->net_idx, rx->dst)) {
            friend_lpn_enqueue_rx(frnd, rx, type, seq_auth, sbuf);
        }
    }
}

bool bt_mesh_friend_enqueue_tx(struct bt_mesh_net_tx *tx,
                               enum bt_mesh_friend_pdu_type type,
                               u64_t *seq_auth, struct net_buf_simple *sbuf)
{
    bool matched = false;
    int i;

    if (!bt_mesh_friend_match(tx->sub->net_idx, tx->ctx->addr) ||
            bt_mesh_friend_get() != BLE_MESH_FRIEND_ENABLED) {
        return matched;
    }

    BT_DBG("net_idx 0x%04x dst 0x%04x src 0x%04x", tx->sub->net_idx,
           tx->ctx->addr, tx->src);

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];

        if (friend_lpn_matches(frnd, tx->sub->net_idx, tx->ctx->addr)) {
            friend_lpn_enqueue_tx(frnd, tx, type, seq_auth, sbuf);
            matched = true;
        }
    }

    return matched;
}

void bt_mesh_friend_clear_incomplete(struct bt_mesh_subnet *sub, u16_t src,
                                     u16_t dst, u64_t *seq_auth)
{
    int i;

    BT_DBG("%s", __func__);

    for (i = 0; i < ARRAY_SIZE(bt_mesh.frnd); i++) {
        struct bt_mesh_friend *frnd = &bt_mesh.frnd[i];
        int j;

        if (!friend_lpn_matches(frnd, sub->net_idx, dst)) {
            continue;
        }

        for (j = 0; j < ARRAY_SIZE(frnd->seg); j++) {
            struct bt_mesh_friend_seg *seg = &frnd->seg[j];
            struct net_buf *buf;

            buf = (void *)sys_slist_peek_head(&seg->queue);
            if (!buf) {
                continue;
            }

            if (BLE_MESH_ADV(buf)->addr != src) {
                continue;
            }

            if (FRIEND_ADV(buf)->seq_auth != *seq_auth) {
                continue;
            }

            BT_WARN("%s, Clearing incomplete segments for 0x%04x", __func__, src);

            while (!sys_slist_is_empty(&seg->queue)) {
                net_buf_unref(net_buf_slist_get(&seg->queue));
            }
        }
    }
}

#endif /* CONFIG_BLE_MESH_FRIEND */
