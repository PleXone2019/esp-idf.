// Copyright 2017-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <errno.h>

#include "btc/btc_manage.h"
#include "osi/allocator.h"

#include "cfg_cli.h"
#include "btc_ble_mesh_generic_model.h"
#include "esp_ble_mesh_generic_model_api.h"

static inline void btc_ble_mesh_cb_to_app(esp_ble_mesh_generic_client_cb_event_t event,
        esp_ble_mesh_generic_client_cb_param_t *param)
{
    esp_ble_mesh_generic_client_cb_t btc_mesh_cb = (esp_ble_mesh_generic_client_cb_t)btc_profile_cb_get(BTC_PID_GENERIC_CLIENT);
    if (btc_mesh_cb) {
        btc_mesh_cb(event, param);
    }
}

void btc_ble_mesh_generic_client_arg_deep_copy(btc_msg_t *msg, void *p_dest, void *p_src)
{
    btc_ble_mesh_generic_client_args_t *dst = (btc_ble_mesh_generic_client_args_t *)p_dest;
    btc_ble_mesh_generic_client_args_t *src = (btc_ble_mesh_generic_client_args_t *)p_src;
    u32_t opcode;
    u16_t length;

    if (!msg || !dst || !src) {
        LOG_ERROR("%s, Invalid parameter", __func__);
        return;
    }

    switch (msg->act) {
    case BTC_BLE_MESH_ACT_GENERIC_CLIENT_GET_STATE: {
        dst->generic_client_get_state.params = (esp_ble_mesh_client_common_param_t *)osi_malloc(sizeof(esp_ble_mesh_client_common_param_t));
        dst->generic_client_get_state.get_state = (esp_ble_mesh_generic_client_get_state_t *)osi_malloc(sizeof(esp_ble_mesh_generic_client_get_state_t));
        if (dst->generic_client_get_state.params && dst->generic_client_get_state.get_state) {
            memcpy(dst->generic_client_get_state.params, src->generic_client_get_state.params,
                   sizeof(esp_ble_mesh_client_common_param_t));
            memcpy(dst->generic_client_get_state.get_state, src->generic_client_get_state.get_state,
                   sizeof(esp_ble_mesh_generic_client_get_state_t));
        } else {
            LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
        }
        break;
    }
    case BTC_BLE_MESH_ACT_GENERIC_CLIENT_SET_STATE: {
        dst->generic_client_set_state.params = (esp_ble_mesh_client_common_param_t *)osi_malloc(sizeof(esp_ble_mesh_client_common_param_t));
        dst->generic_client_set_state.set_state = (esp_ble_mesh_generic_client_set_state_t *)osi_malloc(sizeof(esp_ble_mesh_generic_client_set_state_t));
        if (dst->generic_client_set_state.params && dst->generic_client_set_state.set_state) {
            memcpy(dst->generic_client_set_state.params, src->generic_client_set_state.params,
                   sizeof(esp_ble_mesh_client_common_param_t));
            memcpy(dst->generic_client_set_state.set_state, src->generic_client_set_state.set_state,
                   sizeof(esp_ble_mesh_generic_client_set_state_t));

            opcode = src->generic_client_set_state.params->opcode;
            switch (opcode) {
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET:
                if (src->generic_client_set_state.set_state->user_property_set.property_value) {
                    length = src->generic_client_set_state.set_state->user_property_set.property_value->len;
                    dst->generic_client_set_state.set_state->user_property_set.property_value = bt_mesh_alloc_buf(length);
                    if (!dst->generic_client_set_state.set_state->user_property_set.property_value) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(dst->generic_client_set_state.set_state->user_property_set.property_value, 0);
                    net_buf_simple_add_mem(dst->generic_client_set_state.set_state->user_property_set.property_value,
                                           src->generic_client_set_state.set_state->user_property_set.property_value->data,
                                           src->generic_client_set_state.set_state->user_property_set.property_value->len);
                }
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTY_SET:
                if (src->generic_client_set_state.set_state->admin_property_set.property_value) {
                    length = src->generic_client_set_state.set_state->admin_property_set.property_value->len;
                    dst->generic_client_set_state.set_state->admin_property_set.property_value = bt_mesh_alloc_buf(length);
                    if (!dst->generic_client_set_state.set_state->admin_property_set.property_value) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(dst->generic_client_set_state.set_state->admin_property_set.property_value, 0);
                    net_buf_simple_add_mem(dst->generic_client_set_state.set_state->admin_property_set.property_value,
                                           src->generic_client_set_state.set_state->admin_property_set.property_value->data,
                                           src->generic_client_set_state.set_state->admin_property_set.property_value->len);
                }
                break;
            default:
                break;
            }
        } else {
            LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
        }
        break;
    }
    default:
        LOG_DEBUG("%s, Unknown deep copy act %d", __func__, msg->act);
        break;
    }
}

static void btc_ble_mesh_copy_req_data(btc_msg_t *msg, void *p_dest, void *p_src)
{
    esp_ble_mesh_generic_client_cb_param_t *p_dest_data = (esp_ble_mesh_generic_client_cb_param_t *)p_dest;
    esp_ble_mesh_generic_client_cb_param_t *p_src_data = (esp_ble_mesh_generic_client_cb_param_t *)p_src;
    u32_t opcode;
    u16_t length;

    if (!msg || !p_src_data || !p_dest_data) {
        LOG_ERROR("%s, Invalid parameter", __func__);
        return;
    }

    switch (msg->act) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
        if (p_src_data->params) {
            opcode = p_src_data->params->opcode;
            switch (opcode) {
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTIES_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTIES_STATUS:
                if (p_src_data->status_cb.user_properties_status.property_ids) {
                    length = p_src_data->status_cb.user_properties_status.property_ids->len;
                    p_dest_data->status_cb.user_properties_status.property_ids = bt_mesh_alloc_buf(length);
                    if (!p_dest_data->status_cb.user_properties_status.property_ids) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(p_dest_data->status_cb.user_properties_status.property_ids, 0);
                    net_buf_simple_add_mem(p_dest_data->status_cb.user_properties_status.property_ids,
                                           p_src_data->status_cb.user_properties_status.property_ids->data,
                                           p_src_data->status_cb.user_properties_status.property_ids->len);
                }
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET:
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_STATUS:
                if (p_src_data->status_cb.user_property_status.property_value) {
                    length = p_src_data->status_cb.user_property_status.property_value->len;
                    p_dest_data->status_cb.user_property_status.property_value = bt_mesh_alloc_buf(length);
                    if (!p_dest_data->status_cb.user_property_status.property_value) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(p_dest_data->status_cb.user_property_status.property_value, 0);
                    net_buf_simple_add_mem(p_dest_data->status_cb.user_property_status.property_value,
                                           p_src_data->status_cb.user_property_status.property_value->data,
                                           p_src_data->status_cb.user_property_status.property_value->len);
                }
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTIES_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTIES_STATUS:
                if (p_src_data->status_cb.admin_properties_status.property_ids) {
                    length = p_src_data->status_cb.admin_properties_status.property_ids->len;
                    p_dest_data->status_cb.admin_properties_status.property_ids = bt_mesh_alloc_buf(length);
                    if (!p_dest_data->status_cb.admin_properties_status.property_ids) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(p_dest_data->status_cb.admin_properties_status.property_ids, 0);
                    net_buf_simple_add_mem(p_dest_data->status_cb.admin_properties_status.property_ids,
                                           p_src_data->status_cb.admin_properties_status.property_ids->data,
                                           p_src_data->status_cb.admin_properties_status.property_ids->len);
                }
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTY_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTY_SET:
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTY_STATUS:
                if (p_src_data->status_cb.admin_property_status.property_value) {
                    length = p_src_data->status_cb.admin_property_status.property_value->len;
                    p_dest_data->status_cb.admin_property_status.property_value = bt_mesh_alloc_buf(length);
                    if (!p_dest_data->status_cb.admin_property_status.property_value) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(p_dest_data->status_cb.admin_property_status.property_value, 0);
                    net_buf_simple_add_mem(p_dest_data->status_cb.admin_property_status.property_value,
                                           p_src_data->status_cb.admin_property_status.property_value->data,
                                           p_src_data->status_cb.admin_property_status.property_value->len);
                }
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTIES_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTIES_STATUS:
                if (p_src_data->status_cb.manufacturer_properties_status.property_ids) {
                    length = p_src_data->status_cb.manufacturer_properties_status.property_ids->len;
                    p_dest_data->status_cb.manufacturer_properties_status.property_ids = bt_mesh_alloc_buf(length);
                    if (!p_dest_data->status_cb.manufacturer_properties_status.property_ids) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(p_dest_data->status_cb.manufacturer_properties_status.property_ids, 0);
                    net_buf_simple_add_mem(p_dest_data->status_cb.manufacturer_properties_status.property_ids,
                                           p_src_data->status_cb.manufacturer_properties_status.property_ids->data,
                                           p_src_data->status_cb.manufacturer_properties_status.property_ids->len);
                }
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTY_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTY_SET:
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTY_STATUS:
                if (p_src_data->status_cb.manufacturer_property_status.property_value) {
                    length = p_src_data->status_cb.manufacturer_property_status.property_value->len;
                    p_dest_data->status_cb.manufacturer_property_status.property_value = bt_mesh_alloc_buf(length);
                    if (!p_dest_data->status_cb.manufacturer_property_status.property_value) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(p_dest_data->status_cb.manufacturer_property_status.property_value, 0);
                    net_buf_simple_add_mem(p_dest_data->status_cb.manufacturer_property_status.property_value,
                                           p_src_data->status_cb.manufacturer_property_status.property_value->data,
                                           p_src_data->status_cb.manufacturer_property_status.property_value->len);
                }
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_CLIENT_PROPERTIES_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_CLIENT_PROPERTIES_STATUS:
                if (p_src_data->status_cb.client_properties_status.property_ids) {
                    length = p_src_data->status_cb.client_properties_status.property_ids->len;
                    p_dest_data->status_cb.client_properties_status.property_ids = bt_mesh_alloc_buf(length);
                    if (!p_dest_data->status_cb.client_properties_status.property_ids) {
                        LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
                        return;
                    }
                    net_buf_simple_init(p_dest_data->status_cb.client_properties_status.property_ids, 0);
                    net_buf_simple_add_mem(p_dest_data->status_cb.client_properties_status.property_ids,
                                           p_src_data->status_cb.client_properties_status.property_ids->data,
                                           p_src_data->status_cb.client_properties_status.property_ids->len);
                }
                break;
            default:
                break;
            }
        }
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        if (p_src_data->params) {
            p_dest_data->params = osi_malloc(sizeof(esp_ble_mesh_client_common_param_t));
            if (p_dest_data->params) {
                memcpy(p_dest_data->params, p_src_data->params, sizeof(esp_ble_mesh_client_common_param_t));
            } else {
                LOG_ERROR("%s, Failed to allocate memory, act %d", __func__, msg->act);
            }
        }
        break;
    default:
        break;
    }
}

static void btc_ble_mesh_free_req_data(btc_msg_t *msg)
{
    esp_ble_mesh_generic_client_cb_param_t *arg = NULL;
    u32_t opcode;

    if (!msg || !msg->arg) {
        LOG_ERROR("%s, Invalid parameter", __func__);
        return;
    }

    arg = (esp_ble_mesh_generic_client_cb_param_t *)(msg->arg);

    switch (msg->act) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
        if (arg->params) {
            opcode = arg->params->opcode;
            switch (opcode) {
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTIES_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTIES_STATUS:
                bt_mesh_free_buf(arg->status_cb.user_properties_status.property_ids);
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET:
            case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_STATUS:
                bt_mesh_free_buf(arg->status_cb.user_property_status.property_value);
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTIES_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTIES_STATUS:
                bt_mesh_free_buf(arg->status_cb.admin_properties_status.property_ids);
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTY_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTY_SET:
            case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTY_STATUS:
                bt_mesh_free_buf(arg->status_cb.admin_property_status.property_value);
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTIES_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTIES_STATUS:
                bt_mesh_free_buf(arg->status_cb.manufacturer_properties_status.property_ids);
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTY_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTY_SET:
            case ESP_BLE_MESH_MODEL_OP_GEN_MANUFACTURER_PROPERTY_STATUS:
                bt_mesh_free_buf(arg->status_cb.manufacturer_property_status.property_value);
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_CLIENT_PROPERTIES_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_CLIENT_PROPERTIES_STATUS:
                bt_mesh_free_buf(arg->status_cb.client_properties_status.property_ids);
                break;
            default:
                break;
            }
        }
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        if (arg->params) {
            osi_free(arg->params);
        }
        break;
    default:
        break;
    }
}

void btc_ble_mesh_generic_client_arg_deep_free(btc_msg_t *msg)
{
    btc_ble_mesh_generic_client_args_t *arg = NULL;
    u32_t opcode = 0;

    if (!msg || !msg->arg) {
        LOG_ERROR("%s, Invalid parameter", __func__);
        return;
    }

    arg = (btc_ble_mesh_generic_client_args_t *)(msg->arg);

    switch (msg->act) {
    case BTC_BLE_MESH_ACT_GENERIC_CLIENT_GET_STATE:
        if (arg->generic_client_get_state.params) {
            osi_free(arg->generic_client_get_state.params);
        }
        if (arg->generic_client_get_state.get_state) {
            osi_free(arg->generic_client_get_state.get_state);
        }
        break;
    case BTC_BLE_MESH_ACT_GENERIC_CLIENT_SET_STATE:
        if (arg->generic_client_set_state.params) {
            opcode = arg->generic_client_set_state.params->opcode;
            osi_free(arg->generic_client_set_state.params);
        }
        if (arg->generic_client_set_state.set_state) {
            if (opcode) {
                switch (opcode) {
                case ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET:
                    bt_mesh_free_buf(arg->generic_client_set_state.set_state->user_property_set.property_value);
                    break;
                case ESP_BLE_MESH_MODEL_OP_GEN_ADMIN_PROPERTY_SET:
                    bt_mesh_free_buf(arg->generic_client_set_state.set_state->admin_property_set.property_value);
                    break;
                default:
                    break;
                }
            }
            osi_free(arg->generic_client_set_state.set_state);
        }
        break;
    default:
        break;
    }

    return;
}

static void btc_mesh_generic_client_callback(esp_ble_mesh_generic_client_cb_param_t *cb_params, uint8_t act)
{
    btc_msg_t msg = {0};

    LOG_DEBUG("%s", __func__);

    msg.sig = BTC_SIG_API_CB;
    msg.pid = BTC_PID_GENERIC_CLIENT;
    msg.act = act;

    btc_transfer_context(&msg, cb_params,
                         sizeof(esp_ble_mesh_generic_client_cb_param_t), btc_ble_mesh_copy_req_data);
}

void bt_mesh_callback_generic_status_to_btc(u32_t opcode, u8_t evt_type,
        struct bt_mesh_model *model,
        struct bt_mesh_msg_ctx *ctx,
        const u8_t *val, size_t len)
{
    esp_ble_mesh_generic_client_cb_param_t cb_params = {0};
    esp_ble_mesh_client_common_param_t params = {0};
    size_t length;
    uint8_t act;

    if (!model || !ctx) {
        LOG_ERROR("%s, Invalid parameter", __func__);
        return;
    }

    switch (evt_type) {
    case 0x00:
        act = ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT;
        break;
    case 0x01:
        act = ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT;
        break;
    case 0x02:
        act = ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT;
        break;
    case 0x03:
        act = ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT;
        break;
    default:
        LOG_ERROR("%s, Unknown generic client event type %d", __func__, evt_type);
        return;
    }

    params.opcode = opcode;
    params.model = (esp_ble_mesh_model_t *)model;
    params.ctx.net_idx = ctx->net_idx;
    params.ctx.app_idx = ctx->app_idx;
    params.ctx.addr = ctx->addr;
    params.ctx.recv_ttl = ctx->recv_ttl;
    params.ctx.recv_op = ctx->recv_op;
    params.ctx.recv_dst = ctx->recv_dst;

    cb_params.error_code = 0;
    cb_params.params = &params;

    if (val && len) {
        length = (len <= sizeof(cb_params.status_cb)) ? len : sizeof(cb_params.status_cb);
        memcpy(&cb_params.status_cb, val, length);
    }

    btc_mesh_generic_client_callback(&cb_params, act);
}

void btc_mesh_generic_client_publish_callback(u32_t opcode, struct bt_mesh_model *model,
        struct bt_mesh_msg_ctx *ctx, struct net_buf_simple *buf)
{
    if (!model || !ctx || !buf) {
        LOG_ERROR("%s, Invalid parameter", __func__);
        return;
    }

    bt_mesh_callback_generic_status_to_btc(opcode, 0x02, model, ctx, buf->data, buf->len);
}

void btc_mesh_generic_client_call_handler(btc_msg_t *msg)
{
    esp_ble_mesh_generic_client_cb_param_t generic_client_cb = {0};
    esp_ble_mesh_client_common_param_t *params = NULL;
    btc_ble_mesh_generic_client_args_t *arg = NULL;
    struct bt_mesh_common_param common = {0};
    bt_mesh_role_param_t role_param = {0};

    if (!msg || !msg->arg) {
        LOG_ERROR("%s, Invalid parameter", __func__);
        return;
    }

    arg = (btc_ble_mesh_generic_client_args_t *)(msg->arg);

    switch (msg->act) {
    case BTC_BLE_MESH_ACT_GENERIC_CLIENT_GET_STATE: {
        params = arg->generic_client_get_state.params;
        role_param.model = (struct bt_mesh_model *)params->model;
        role_param.role = params->msg_role;
        if (bt_mesh_set_model_role(&role_param)) {
            LOG_ERROR("%s, Failed to set model role", __func__);
            return;
        }
        common.opcode = params->opcode;
        common.model = (struct bt_mesh_model *)params->model;
        common.ctx.net_idx = params->ctx.net_idx;
        common.ctx.app_idx = params->ctx.app_idx;
        common.ctx.addr = params->ctx.addr;
        common.ctx.send_rel = params->ctx.send_rel;
        common.ctx.send_ttl = params->ctx.send_ttl;
        common.msg_timeout = params->msg_timeout;

        generic_client_cb.params = arg->generic_client_get_state.params;
        generic_client_cb.error_code =
            bt_mesh_generic_client_get_state(&common,
                                             (void *)arg->generic_client_get_state.get_state,
                                             (void *)&generic_client_cb.status_cb);
        if (generic_client_cb.error_code) {
            /* If send failed, callback error_code to app layer immediately */
            btc_mesh_generic_client_callback(&generic_client_cb,
                                             ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT);
        }
        break;
    }
    case BTC_BLE_MESH_ACT_GENERIC_CLIENT_SET_STATE: {
        params = arg->generic_client_set_state.params;
        role_param.model = (struct bt_mesh_model *)params->model;
        role_param.role = params->msg_role;
        if (bt_mesh_set_model_role(&role_param)) {
            LOG_ERROR("%s, Failed to set model role", __func__);
            return;
        }
        common.opcode = params->opcode;
        common.model = (struct bt_mesh_model *)params->model;
        common.ctx.net_idx = params->ctx.net_idx;
        common.ctx.app_idx = params->ctx.app_idx;
        common.ctx.addr = params->ctx.addr;
        common.ctx.send_rel = params->ctx.send_rel;
        common.ctx.send_ttl = params->ctx.send_ttl;
        common.msg_timeout = params->msg_timeout;

        generic_client_cb.params = arg->generic_client_set_state.params;
        generic_client_cb.error_code =
            bt_mesh_generic_client_set_state(&common,
                                             (void *)arg->generic_client_set_state.set_state,
                                             (void *)&generic_client_cb.status_cb);
        if (generic_client_cb.error_code) {
            /* If send failed, callback error_code to app layer immediately */
            btc_mesh_generic_client_callback(&generic_client_cb,
                                             ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT);
        }
        break;
    }
    default:
        break;
    }

    btc_ble_mesh_generic_client_arg_deep_free(msg);
}

void btc_mesh_generic_client_cb_handler(btc_msg_t *msg)
{
    esp_ble_mesh_generic_client_cb_param_t *param = NULL;

    if (!msg || !msg->arg) {
        LOG_ERROR("%s, Invalid parameter", __func__);
        return;
    }

    param = (esp_ble_mesh_generic_client_cb_param_t *)(msg->arg);

    if (msg->act < ESP_BLE_MESH_GENERIC_CLIENT_EVT_MAX) {
        btc_ble_mesh_cb_to_app(msg->act, param);
    } else {
        LOG_ERROR("%s, Unknown msg->act = %d", __func__, msg->act);
    }

    btc_ble_mesh_free_req_data(msg);
}
