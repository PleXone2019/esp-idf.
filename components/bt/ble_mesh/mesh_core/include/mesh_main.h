/** @file
 *  @brief Bluetooth Mesh Profile APIs.
 */

/*
 * Copyright (c) 2017 Intel Corporation
 * Additional Copyright (c) 2018 Espressif Systems (Shanghai) PTE LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _BLE_MESH_MAIN_H_
#define _BLE_MESH_MAIN_H_

#include "mesh_util.h"
#include "mesh_access.h"

/**
 * @brief Bluetooth Mesh Provisioning
 * @defgroup bt_mesh_prov Bluetooth Mesh Provisioning
 * @ingroup bt_mesh
 * @{
 */

typedef enum {
    BLE_MESH_NO_OUTPUT       = 0,
    BLE_MESH_BLINK           = BIT(0),
    BLE_MESH_BEEP            = BIT(1),
    BLE_MESH_VIBRATE         = BIT(2),
    BLE_MESH_DISPLAY_NUMBER  = BIT(3),
    BLE_MESH_DISPLAY_STRING  = BIT(4),
} bt_mesh_output_action_t;

typedef enum {
    BLE_MESH_NO_INPUT      = 0,
    BLE_MESH_PUSH          = BIT(0),
    BLE_MESH_TWIST         = BIT(1),
    BLE_MESH_ENTER_NUMBER  = BIT(2),
    BLE_MESH_ENTER_STRING  = BIT(3),
} bt_mesh_input_action_t;

typedef enum {
    BLE_MESH_PROV_ADV   = BIT(0),
    BLE_MESH_PROV_GATT  = BIT(1),
} bt_mesh_prov_bearer_t;

/** Provisioning properties & capabilities. */
struct bt_mesh_prov {
#if CONFIG_BLE_MESH_NODE
    /** The UUID that's used when advertising as unprovisioned */
    const u8_t *uuid;

    /** Flag indicates whether unprovisioned devices support OOB public key */
    bool oob_pub_key;

    /** @brief Set device OOB public key.
     *
     *  This callback notifies the application to
     *  set OOB public key & private key pair.
     */
    void (*oob_pub_key_cb)(void);

    /** Static OOB value */
    const u8_t *static_val;
    /** Static OOB value length */
    u8_t        static_val_len;

    /** Maximum size of Output OOB supported */
    u8_t        output_size;
    /** Supported Output OOB Actions */
    u16_t       output_actions;

    /* Maximum size of Input OOB supported */
    u8_t        input_size;
    /** Supported Input OOB Actions */
    u16_t       input_actions;

    /** @brief Output of a number is requested.
     *
     *  This callback notifies the application to
     *  output the given number using the given action.
     *
     *  @param act Action for outputting the number.
     *  @param num Number to be out-put.
     *
     *  @return Zero on success or negative error code otherwise
     */
    int         (*output_number)(bt_mesh_output_action_t act, u32_t num);

    /** @brief Output of a string is requested.
     *
     *  This callback notifies the application to
     *  display the given string to the user.
     *
     *  @param str String to be displayed.
     *
     *  @return Zero on success or negative error code otherwise
     */
    int         (*output_string)(const char *str);

    /** @brief Input is requested.
     *
     *  This callback notifies the application to request
     *  input from the user using the given action. The
     *  requested input will either be a string or a number, and
     *  the application needs to consequently call the
     *  bt_mesh_input_string() or bt_mesh_input_number() functions
     *  once the data has been acquired from the user.
     *
     *  @param act Action for inputting data.
     *  @param num Maximum size of the in-put data.
     *
     *  @return Zero on success or negative error code otherwise
     */
    int         (*input)(bt_mesh_input_action_t act, u8_t size);

    /** @brief Provisioning link has been opened.
     *
     *  This callback notifies the application that a provisioning
     *  link has been opened on the given provisioning bearer.
     *
     *  @param bearer Provisioning bearer.
     */
    void        (*link_open)(bt_mesh_prov_bearer_t bearer);

    /** @brief Provisioning link has been closed.
     *
     *  This callback notifies the application that a provisioning
     *  link has been closed on the given provisioning bearer.
     *
     *  @param bearer Provisioning bearer.
     */
    void        (*link_close)(bt_mesh_prov_bearer_t bearer);

    /** @brief Provisioning is complete.
     *
     *  This callback notifies the application that provisioning has
     *  been successfully completed, and that the local node has been
     *  assigned the specified NetKeyIndex and primary element address.
     *
     *  @param net_idx  NetKeyIndex given during provisioning.
     *  @param addr     Primary element address.
     *  @param flags    Key Refresh & IV Update flags
     *  @param iv_index IV Index.
     */
    void        (*complete)(u16_t net_idx, u16_t addr, u8_t flags, u32_t iv_index);

    /** @brief Node has been reset.
     *
     *  This callback notifies the application that the local node
     *  has been reset and needs to be reprovisioned. The node will
     *  not automatically advertise as unprovisioned, rather the
     *  bt_mesh_prov_enable() API needs to be called to enable
     *  unprovisioned advertising on one or more provisioning bearers.
     */
    void        (*reset)(void);
#endif /* CONFIG_BLE_MESH_NODE */

#if CONFIG_BLE_MESH_PROVISIONER
    /* Provisioner device uuid */
    const u8_t *prov_uuid;

    /*
     * Primary element address of the provisioner.
     * No need to initialize it for fast provisioning.
     */
    const u16_t prov_unicast_addr;

    /*
     * Starting unicast address going to assigned.
     * No need to initialize it for fast provisioning.
     */
    u16_t prov_start_address;

    /* Attention timer contained in Provisioning Invite */
    u8_t  prov_attention;

    /* Provisioner provisioning Algorithm */
    u8_t  prov_algorithm;

    /* Provisioner public key oob */
    u8_t  prov_pub_key_oob;

    /** @brief Input is requested.
     *
     *  This callback notifies the application that it should
     *  read device's public key with OOB
     *
     *  @param link_idx: The provisioning link index
     *
     *  @return Zero on success or negative error code otherwise
     */
    int  (*prov_pub_key_oob_cb)(u8_t link_idx);

    /* Provisioner static oob value */
    u8_t *prov_static_oob_val;

    /* Provisioner static oob value length */
    u8_t  prov_static_oob_len;

    /** @brief Provisioner input a number read from device output
     *
     *  This callback notifies the application that it should
     *  input the number given by the device.
     *
     *  @param method:   The OOB authentication method
     *  @param act:      The output action of the device
     *  @param size:     The output size of the device
     *  @param link_idx: The provisioning link index
     *
     *  @return Zero on success or negative error code otherwise
     */
    int  (*prov_input_num)(u8_t method, bt_mesh_output_action_t act, u8_t size, u8_t link_idx);

    /** @brief Provisioner output a number to the device
     *
     *  This callback notifies the application that it should
     *  output the number to the device.
     *
     *  @param method:   The OOB authentication method
     *  @param act:      The input action of the device
     *  @param data:     The input number/string of the device
     *  @param size:     The input size of the device
     *  @param link_idx: The provisioning link index
     *
     *  @return Zero on success or negative error code otherwise
     */
    int  (*prov_output_num)(u8_t method, bt_mesh_input_action_t act, void *data, u8_t size, u8_t link_idx);

    /*
     * Key refresh and IV update flag.
     * No need to initialize it for fast provisioning.
     */
    u8_t  flags;

    /*
     * IV index. No need to initialize it for fast provisioning.
     */
    u32_t iv_index;

    /** @brief Provisioner has opened a provisioning link.
     *
     *  This callback notifies the application that a provisioning
     *  link has been opened on the given provisioning bearer.
     *
     *  @param bearer Provisioning bearer.
     */
    void (*prov_link_open)(bt_mesh_prov_bearer_t bearer);

    /** @brief Provisioner has closed a provisioning link.
     *
     *  This callback notifies the application that a provisioning
     *  link has been closed on the given provisioning bearer.
     *
     *  @param bearer Provisioning bearer.
     *  @param reason Provisioning link close reason(disconnect reason)
     *                0xFF: disconnect due to provisioner_pb_gatt_disable()
     */
    void (*prov_link_close)(bt_mesh_prov_bearer_t bearer, u8_t reason);

    /** @brief Provision one device is complete.
     *
     *  This callback notifies the application that provisioner has
     *  successfully provisioned a device, and the node has been assigned
     *  the specified NetKeyIndex and primary element address.
     *
     *  @param node_idx     Node index within the node(provisioned device) queue.
     *  @param device_uuid  Provisioned device uuid pointer.
     *  @param unicast_addr Provisioned device assigned unicast address.
     *  @param element_num  Provisioned device element number.
     *  @param netkey_idx   Provisioned device assigned netkey index.
     */
    void (*prov_complete)(int node_idx, const u8_t device_uuid[16],
                          u16_t unicast_addr, u8_t element_num,
                          u16_t netkey_idx);

#endif /* CONFIG_BLE_MESH_PROVISIONER */
};

enum ble_mesh_role {
    NODE = 0,
    PROVISIONER,
    FAST_PROV,
    ROLE_NVAL,
};

/** @brief Provide provisioning input OOB string.
 *
 *  This is intended to be called after the bt_mesh_prov input callback
 *  has been called with BLE_MESH_ENTER_STRING as the action.
 *
 *  @param str String.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_input_string(const char *str);

/** @brief Provide provisioning input OOB number.
 *
 *  This is intended to be called after the bt_mesh_prov input callback
 *  has been called with BLE_MESH_ENTER_NUMBER as the action.
 *
 *  @param num Number.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_input_number(u32_t num);

/** @brief Enable specific provisioning bearers
 *
 *  Enable one or more provisioning bearers.
 *
 *  @param Bit-wise OR of provisioning bearers.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_prov_enable(bt_mesh_prov_bearer_t bearers);

/** @brief Disable specific provisioning bearers
 *
 *  Disable one or more provisioning bearers.
 *
 *  @param Bit-wise OR of provisioning bearers.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_prov_disable(bt_mesh_prov_bearer_t bearers);

/** @brief Indicate whether provisioner is enabled
 *
 *  @return true - enabled, false - disabled.
 */
bool bt_mesh_is_provisioner_en(void);

/* The following API is for fast provisioning */

#if CONFIG_BLE_MESH_FAST_PROV

/** @brief Change the device action
 *
 *  @param[IN] action: role of device to be set
 *                     0x01 - enter, 0x02 - suspend, 0x03 - exit
 *
 *  @return status
 */
u8_t bt_mesh_set_fast_prov_action(u8_t action);

#endif /* CONFIG_BLE_MESH_FAST_PROV */

#if CONFIG_BLE_MESH_PROVISIONER

/** @brief Provide provisioning input OOB string.
 *
 *  This is intended to be called after the bt_mesh_prov input callback
 *  has been called with BLE_MESH_ENTER_STRING as the action.
 *
 *  @param str String.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_prov_input_string(const char *str);

/** @brief Provide provisioning input OOB number.
 *
 *  This is intended to be called after the bt_mesh_prov input callback
 *  has been called with BLE_MESH_ENTER_NUMBER as the action.
 *
 *  @param num Number.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_prov_input_number(u32_t num);

/** @brief Enable specific provisioning bearers
 *
 *  Enable one or more provisioning bearers.
 *
 *  @param bearers Bit-wise OR of provisioning bearers.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_provisioner_enable(bt_mesh_prov_bearer_t bearers);

/** @brief Disable specific provisioning bearers
 *
 *  Disable one or more provisioning bearers.
 *
 *  @param bearers Bit-wise OR of provisioning bearers.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_provisioner_disable(bt_mesh_prov_bearer_t bearers);

#endif /* CONFIG_BLE_MESH_PROVISIONER */

/**
 * @}
 */

/**
 * @brief Bluetooth Mesh
 * @defgroup bt_mesh Bluetooth Mesh
 * @ingroup bluetooth
 * @{
 */

/* Primary Network Key index */
#define BLE_MESH_NET_PRIMARY                 0x000

#define BLE_MESH_RELAY_DISABLED              0x00
#define BLE_MESH_RELAY_ENABLED               0x01
#define BLE_MESH_RELAY_NOT_SUPPORTED         0x02

#define BLE_MESH_BEACON_DISABLED             0x00
#define BLE_MESH_BEACON_ENABLED              0x01

#define BLE_MESH_GATT_PROXY_DISABLED         0x00
#define BLE_MESH_GATT_PROXY_ENABLED          0x01
#define BLE_MESH_GATT_PROXY_NOT_SUPPORTED    0x02

#define BLE_MESH_FRIEND_DISABLED             0x00
#define BLE_MESH_FRIEND_ENABLED              0x01
#define BLE_MESH_FRIEND_NOT_SUPPORTED        0x02

#define BLE_MESH_NODE_IDENTITY_STOPPED       0x00
#define BLE_MESH_NODE_IDENTITY_RUNNING       0x01
#define BLE_MESH_NODE_IDENTITY_NOT_SUPPORTED 0x02

/* Features */
#define BLE_MESH_FEAT_RELAY                  BIT(0)
#define BLE_MESH_FEAT_PROXY                  BIT(1)
#define BLE_MESH_FEAT_FRIEND                 BIT(2)
#define BLE_MESH_FEAT_LOW_POWER              BIT(3)
#define BLE_MESH_FEAT_SUPPORTED              (BLE_MESH_FEAT_RELAY |     \
                                              BLE_MESH_FEAT_PROXY |     \
                                              BLE_MESH_FEAT_FRIEND |    \
                                              BLE_MESH_FEAT_LOW_POWER)

/** @brief Initialize Mesh support
 *
 *  After calling this API, the node will not automatically advertise as
 *  unprovisioned, rather the bt_mesh_prov_enable() API needs to be called
 *  to enable unprovisioned advertising on one or more provisioning bearers.
 *
 *  @param prov Node provisioning information.
 *  @param comp Node Composition.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_init(const struct bt_mesh_prov *prov,
                 const struct bt_mesh_comp *comp);

/** @brief Reset the state of the local Mesh node.
 *
 *  Resets the state of the node, which means that it needs to be
 *  reprovisioned to become an active node in a Mesh network again.
 *
 *  After calling this API, the node will not automatically advertise as
 *  unprovisioned, rather the bt_mesh_prov_enable() API needs to be called
 *  to enable unprovisioned advertising on one or more provisioning bearers.
 *
 */
void bt_mesh_reset(void);

/** @brief Provision the local Mesh Node.
 *
 *  This API should normally not be used directly by the application. The
 *  only exception is for testing purposes where manual provisioning is
 *  desired without an actual external provisioner.
 *
 *  @param net_key  Network Key
 *  @param net_idx  Network Key Index
 *  @param flags    Provisioning Flags
 *  @param iv_index IV Index
 *  @param seq      Sequence Number (0 if newly provisioned).
 *  @param addr     Primary element address
 *  @param dev_key  Device Key
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_provision(const u8_t net_key[16], u16_t net_idx,
                      u8_t flags, u32_t iv_index, u32_t seq,
                      u16_t addr, const u8_t dev_key[16]);

/** @brief Toggle the IV Update test mode
 *
 *  This API is only available if the IV Update test mode has been enabled
 *  in Kconfig. It is needed for passing most of the IV Update qualification
 *  test cases.
 *
 *  @param enable true to enable IV Update test mode, false to disable it.
 */
void bt_mesh_iv_update_test(bool enable);

/** @brief Toggle the IV Update state
 *
 *  This API is only available if the IV Update test mode has been enabled
 *  in Kconfig. It is needed for passing most of the IV Update qualification
 *  test cases.
 *
 *  @return true if IV Update In Progress state was entered, false otherwise.
 */
bool bt_mesh_iv_update(void);

/** @brief Toggle the Low Power feature of the local device
 *
 *  Enables or disables the Low Power feature of the local device. This is
 *  exposed as a run-time feature, since the device might want to change
 *  this e.g. based on being plugged into a stable power source or running
 *  from a battery power source.
 *
 *  @param enable  true to enable LPN functionality, false to disable it.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_lpn_set(bool enable);

/** @brief Send out a Friend Poll message.
 *
 *  Send a Friend Poll message to the Friend of this node. If there is no
 *  established Friendship the function will return an error.
 *
 *  @return Zero on success or (negative) error code otherwise.
 */
int bt_mesh_lpn_poll(void);

/** @brief Register a callback for Friendship changes.
 *
 *  Registers a callback that will be called whenever Friendship gets
 *  established or is lost.
 *
 *  @param cb Function to call when the Friendship status changes.
 */
void bt_mesh_lpn_set_cb(void (*cb)(u16_t friend_addr, bool established));

/**
 * @}
 */

#endif /* _BLE_MESH_MAIN_H_ */
