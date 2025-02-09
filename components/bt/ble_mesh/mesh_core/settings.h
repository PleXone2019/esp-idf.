/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include "sdkconfig.h"

#include "net.h"
#include "mesh_access.h"
#include "mesh_bearer_adapt.h"
#include "provisioner_main.h"

int settings_core_init(void);
int settings_core_load(void);
int settings_core_commit(void);

void bt_mesh_store_role(u8_t role);
void bt_mesh_store_net(void);
void bt_mesh_store_iv(void);
void bt_mesh_store_seq(void);
void bt_mesh_store_rpl(struct bt_mesh_rpl *rpl);
void bt_mesh_store_subnet(struct bt_mesh_subnet *sub);
void bt_mesh_store_app_key(struct bt_mesh_app_key *key);
void bt_mesh_store_hb_pub(void);
void bt_mesh_store_cfg(void);
void bt_mesh_store_mod_bind(struct bt_mesh_model *mod);
void bt_mesh_store_mod_sub(struct bt_mesh_model *mod);
void bt_mesh_store_mod_pub(struct bt_mesh_model *mod);

void bt_mesh_clear_role(void);
void bt_mesh_clear_net(void);
void bt_mesh_clear_subnet(struct bt_mesh_subnet *sub);
void bt_mesh_clear_app_key(struct bt_mesh_app_key *key);
void bt_mesh_clear_rpl(void);

#if CONFIG_BLE_MESH_PROVISIONER
void bt_mesh_store_p_net_idx(void);
void bt_mesh_store_p_app_idx(void);
void bt_mesh_store_p_subnet(struct bt_mesh_subnet *sub);
void bt_mesh_store_p_app_key(struct bt_mesh_app_key *key);
void bt_mesh_clear_p_subnet(struct bt_mesh_subnet *sub);
void bt_mesh_clear_p_app_key(struct bt_mesh_app_key *key);
void bt_mesh_clear_rpl_single(u16_t src);
void bt_mesh_store_node_info(struct bt_mesh_node_t *node, bool prov);
void bt_mesh_clear_node_info(u16_t unicast_addr, bool prov);
#endif

int bt_mesh_settings_init(void);

#endif /* _SETTINGS_H_ */
