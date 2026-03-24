/**
 * sle_task server implementation
 *
 * 迁移原则：
 * 1. 对外接口名、初始化顺序、模块切分对齐 sle_uart_demo server 侧。
 * 2. 去掉 AppSle* 这一层应用封装，直接暴露 sample 风格接口。
 * 3. 保留少量必要修正，避免照抄样例里的明显缺陷影响实际连接和发送。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "securec.h"
#include "sle_common.h"
#include "sle_errcode.h"
#include "osal_task.h"
#include "sle_ssap_server.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_task_adv.h"
#include "sle_task.h"
#include "cmsis_os2.h"
#include "ohos_init.h"
#include "ohos_sle_common.h"
#include "ohos_sle_device_discovery.h"
#include "ohos_sle_connection_manager.h"
#include "iot_uart.h"
#include "pinctrl.h"
#include "uart.h"
#include "errcode.h"

#define OCTET_BIT_LEN               8
#define UUID_LEN_2                  2
#define UUID_INDEX                  14
#define BT_INDEX_5                  5
#define BT_INDEX_4                  4
#define BT_INDEX_0                  0
#define UART_BUFF_LENGTH            0x100
#define SLE_MTU_SIZE_DEFAULT        520
#define SLE_SERVER_INIT_DELAY_MS    1000
#define SLE_SEND_MUTEX_TIMEOUT_MS   100

#define UUID_16BIT_LEN 2
#define UUID_128BIT_LEN 16
#define SLE_UUID_SERVER_SERVICE        0x2222
#define SLE_UUID_SERVER_NTF_REPORT     0x2323
#define SLE_UUID_TEST_PROPERTIES       (SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE)
#define SLE_UUID_TEST_OPERATION_INDICATION \
    (SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE | SSAP_OPERATE_INDICATION_BIT_NOTIFY)
#define SLE_UUID_TEST_DESCRIPTOR       (SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE)
#define sample_at_log_print(fmt, args...) printf(fmt, ##args)
#define SLE_UART_SERVER_LOG "[sle uart server]"

/* sle server app uuid for test */
static uint8_t g_sle_uuid_app_uuid[UUID_LEN_2] = { 0x12, 0x34 };
/* server notify property uuid for test */
static uint8_t g_sle_property_value[OCTET_BIT_LEN] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
/* sle connect acb handle */
static volatile uint16_t g_sle_conn_hdl = 0;
/* sle server handle */
static uint8_t g_server_id = 0;
/* sle service handle */
static uint16_t g_service_handle = 0;
/* sle ntf property handle */
static uint16_t g_property_handle = 0;
/* sle pair acb handle */
static volatile uint16_t g_sle_pair_hdl = 0;
/* sle connected state */
static volatile bool g_sle_connected = false;
/* init guard */
static volatile bool g_sle_init_started = false;

static sle_uart_server_msg_queue g_sle_uart_server_msg_queue = NULL;
static uint8_t g_sle_uart_base[] = {
    0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
    0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#define SLE_UART_TRANSFER_SIZE 256
static uint8_t g_app_uart_rx_buff[SLE_UART_TRANSFER_SIZE] = { 0 };
static osMutexId_t g_sle_send_mutex = NULL;
static uint8_t g_sle_send_buffer[UART_BUFF_LENGTH] = { 0 };

static uart_buffer_config_t g_app_uart_buffer_config = {
    .rx_buffer = g_app_uart_rx_buff,
    .rx_buffer_size = SLE_UART_TRANSFER_SIZE
};

static uint32_t sle_ms_to_ticks_ceil(uint32_t ms)
{
    uint32_t tick_freq = osKernelGetTickFreq();

    if (tick_freq == 0U) {
        return 1U;
    }

    return (uint32_t)((((uint64_t)ms * tick_freq) + 999ULL) / 1000ULL);
}

static void server_uart_rx_callback(const void *buffer, uint16_t length, bool error)
{
    errcode_t ret;

    (void)error;
    if (buffer == NULL || length == 0U) {
        return;
    }

    if (length > UINT8_MAX) {
        sample_at_log_print("%s uart rx too large:%u\r\n", SLE_UART_SERVER_LOG, length);
        return;
    }

    ret = (errcode_t)uart_sle_send_data((uint8_t *)buffer, (uint8_t)length);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_server_send_data_fail:%u\r\n", SLE_UART_SERVER_LOG, ret);
    }
}

static void uart_init_config(void)
{
    uart_attr_t attr = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };

    uart_pin_config_t pin_config = {
        .tx_pin = 0,
        .rx_pin = 0,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };

    (void)uapi_uart_deinit(0);
    (void)uapi_uart_init(0, &pin_config, &attr, NULL, &g_app_uart_buffer_config);
    (void)uapi_uart_register_rx_callback(0, UART_RX_CONDITION_FULL_OR_IDLE, 1, server_uart_rx_callback);
}

static void encode2byte_little(uint8_t *ptr, uint16_t data)
{
    if (ptr == NULL) {
        return;
    }

    ptr[0] = (uint8_t)data;
    ptr[1] = (uint8_t)(data >> 8);
}

static void sle_uuid_set_base(sle_uuid_t *out)
{
    errcode_t ret;

    if (out == NULL) {
        return;
    }

    ret = memcpy_s(out->uuid, SLE_UUID_LEN, g_sle_uart_base, SLE_UUID_LEN);
    if (ret != EOK) {
        sample_at_log_print("%s sle_uuid_set_base memcpy fail\r\n", SLE_UART_SERVER_LOG);
        out->len = 0;
        return;
    }
    out->len = UUID_LEN_2;
}

static void sle_uuid_setu2(uint16_t u2, sle_uuid_t *out)
{
    if (out == NULL) {
        return;
    }

    sle_uuid_set_base(out);
    if (out->len == 0U) {
        return;
    }
    encode2byte_little(&out->uuid[UUID_INDEX], u2);
}

static void sle_uart_uuid_print(const sle_uuid_t *uuid)
{
    if (uuid == NULL) {
        sample_at_log_print("%s uuid_print,uuid is null\r\n", SLE_UART_SERVER_LOG);
        return;
    }
    if (uuid->len == UUID_16BIT_LEN) {
        sample_at_log_print("%s uuid: %02x %02x.\r\n", SLE_UART_SERVER_LOG, uuid->uuid[14], uuid->uuid[15]);
    } else if (uuid->len == UUID_128BIT_LEN) {
        sample_at_log_print("%s uuid:\r\n", SLE_UART_SERVER_LOG);
        sample_at_log_print("%s 0x%02x 0x%02x 0x%02x\r\n", SLE_UART_SERVER_LOG, uuid->uuid[0], uuid->uuid[1],
            uuid->uuid[2]);
        sample_at_log_print("%s 0x%02x 0x%02x 0x%02x\r\n", SLE_UART_SERVER_LOG, uuid->uuid[4], uuid->uuid[5],
            uuid->uuid[6]);
        sample_at_log_print("%s 0x%02x 0x%02x 0x%02x\r\n", SLE_UART_SERVER_LOG, uuid->uuid[8], uuid->uuid[9],
            uuid->uuid[10]);
        sample_at_log_print("%s 0x%02x 0x%02x 0x%02x\r\n", SLE_UART_SERVER_LOG, uuid->uuid[12], uuid->uuid[13],
            uuid->uuid[14]);
    }
}

static void ssaps_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id, ssap_exchange_info_t *mtu_size,
    errcode_t status)
{
    uint32_t mtu_size_value = 0U;

    if (mtu_size != NULL) {
        mtu_size_value = mtu_size->mtu_size;
    }

    sample_at_log_print("%s ssaps mtu changed server_id:%x, conn_id:%x, mtu_size:%lx, status:%x\r\n",
        SLE_UART_SERVER_LOG, server_id, conn_id, (unsigned long)mtu_size_value, status);
    if (g_sle_pair_hdl == 0U) {
        g_sle_pair_hdl = (uint16_t)(conn_id + 1U);
        __sync_synchronize();
    }
}

static void ssaps_start_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    sample_at_log_print("%s start service cbk callback server_id:%d, handle:%x, status:%x\r\n",
        SLE_UART_SERVER_LOG, server_id, handle, status);
}

static void ssaps_add_service_cbk(uint8_t server_id, sle_uuid_t *uuid, uint16_t handle, errcode_t status)
{
    sample_at_log_print("%s add service cbk callback server_id:%x, handle:%x, status:%x\r\n",
        SLE_UART_SERVER_LOG, server_id, handle, status);
    sle_uart_uuid_print(uuid);
}

static void ssaps_add_property_cbk(uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle,
    uint16_t handle, errcode_t status)
{
    sample_at_log_print("%s add property cbk callback server_id:%x, service_handle:%x,handle:%x, status:%x\r\n",
        SLE_UART_SERVER_LOG, server_id, service_handle, handle, status);
    sle_uart_uuid_print(uuid);
}

static void ssaps_add_descriptor_cbk(uint8_t server_id, sle_uuid_t *uuid, uint16_t service_handle,
    uint16_t property_handle, errcode_t status)
{
    sample_at_log_print("%s add descriptor cbk callback server_id:%x, service_handle:%x, property_handle:%x, "
        "status:%x\r\n", SLE_UART_SERVER_LOG, server_id, service_handle, property_handle, status);
    sle_uart_uuid_print(uuid);
}

static void ssaps_delete_all_service_cbk(uint8_t server_id, errcode_t status)
{
    sample_at_log_print("%s delete all service callback server_id:%x, status:%x\r\n",
        SLE_UART_SERVER_LOG, server_id, status);
}

static errcode_t sle_ssaps_register_cbks(ssaps_read_request_callback ssaps_read_callback,
    ssaps_write_request_callback ssaps_write_callback)
{
    errcode_t ret;
    ssaps_callbacks_t ssaps_cbk = { 0 };

    ssaps_cbk.add_service_cb = ssaps_add_service_cbk;
    ssaps_cbk.add_property_cb = ssaps_add_property_cbk;
    ssaps_cbk.add_descriptor_cb = ssaps_add_descriptor_cbk;
    ssaps_cbk.start_service_cb = ssaps_start_service_cbk;
    ssaps_cbk.delete_all_service_cb = ssaps_delete_all_service_cbk;
    ssaps_cbk.mtu_changed_cb = ssaps_mtu_changed_cbk;
    ssaps_cbk.read_request_cb = ssaps_read_callback;
    ssaps_cbk.write_request_cb = ssaps_write_callback;

    ret = ssaps_register_callbacks(&ssaps_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_ssaps_register_cbks,ssaps_register_callbacks fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_uuid_server_service_add(void)
{
    errcode_t ret;
    sle_uuid_t service_uuid = { 0 };

    sle_uuid_setu2(SLE_UUID_SERVER_SERVICE, &service_uuid);
    ret = ssaps_add_service_sync(g_server_id, &service_uuid, true, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle uuid add service fail, ret:%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_uuid_server_property_add(void)
{
    errcode_t ret;
    ssaps_property_info_t property = { 0 };
    ssaps_desc_info_t descriptor = { 0 };
    uint8_t ntf_value[] = { 0x01, 0x00 };

    property.permissions = SLE_UUID_TEST_PROPERTIES;
    property.operate_indication = SLE_UUID_TEST_OPERATION_INDICATION;
    sle_uuid_setu2(SLE_UUID_SERVER_NTF_REPORT, &property.uuid);
    property.value_len = OCTET_BIT_LEN;
    property.value = g_sle_property_value;

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle uart add property fail, ret:%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }

    descriptor.permissions = SLE_UUID_TEST_DESCRIPTOR;
    descriptor.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    descriptor.operate_indication = SLE_UUID_TEST_OPERATION_INDICATION;
    descriptor.value_len = sizeof(ntf_value);
    descriptor.value = ntf_value;

    ret = ssaps_add_descriptor_sync(g_server_id, g_service_handle, g_property_handle, &descriptor);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle uart add descriptor fail, ret:%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_uart_server_add(void)
{
    errcode_t ret;
    sle_uuid_t app_uuid = { 0 };

    sample_at_log_print("%s sle uart add service in\r\n", SLE_UART_SERVER_LOG);
    app_uuid.len = sizeof(g_sle_uuid_app_uuid);
    if (memcpy_s(app_uuid.uuid, sizeof(app_uuid.uuid), g_sle_uuid_app_uuid, sizeof(g_sle_uuid_app_uuid)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }

    ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s ssaps_register_server fail, ret:%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }

    if (sle_uuid_server_service_add() != ERRCODE_SLE_SUCCESS) {
        (void)ssaps_unregister_server(g_server_id);
        return ERRCODE_SLE_FAIL;
    }
    if (sle_uuid_server_property_add() != ERRCODE_SLE_SUCCESS) {
        (void)ssaps_unregister_server(g_server_id);
        return ERRCODE_SLE_FAIL;
    }

    sample_at_log_print("%s sle uart add service, server_id:%x, service_handle:%x, property_handle:%x\r\n",
        SLE_UART_SERVER_LOG, g_server_id, g_service_handle, g_property_handle);
    ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle uart add service fail, ret:%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }
    sample_at_log_print("%s sle uart add service out\r\n", SLE_UART_SERVER_LOG);
    return ERRCODE_SLE_SUCCESS;
}

errcode_t sle_uart_server_send_report_by_handle(const uint8_t *data, uint8_t len)
{
    errcode_t ret;
    ssaps_ntf_ind_t param = { 0 };
    uint32_t timeout_ticks;

    if (data == NULL || len == 0U || len > UART_BUFF_LENGTH) {
        return ERRCODE_INVALID_PARAM;
    }

    if (g_sle_send_mutex == NULL) {
        return ERRCODE_FAIL;
    }

    timeout_ticks = sle_ms_to_ticks_ceil(SLE_SEND_MUTEX_TIMEOUT_MS);
    if (osMutexAcquire(g_sle_send_mutex, timeout_ticks) != osOK) {
        return ERRCODE_FAIL;
    }

    __sync_synchronize();
    if (!g_sle_connected || g_property_handle == 0U) {
        (void)osMutexRelease(g_sle_send_mutex);
        return ERRCODE_FAIL;
    }

    if (memcpy_s(g_sle_send_buffer, sizeof(g_sle_send_buffer), data, len) != EOK) {
        (void)osMutexRelease(g_sle_send_mutex);
        return ERRCODE_MEMCPY;
    }

    param.handle = g_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = g_sle_send_buffer;
    param.value_len = len;

    ret = ssaps_notify_indicate(g_server_id, g_sle_conn_hdl, &param);
    (void)osMutexRelease(g_sle_send_mutex);
    return ret;
}

errcode_t sle_uart_server_send_report_by_uuid(const uint8_t *data, uint8_t len)
{
    return sle_uart_server_send_report_by_handle(data, len);
}

static void sle_connect_state_changed_cbk(uint16_t conn_id, const SleAddr *addr, SleAcbStateType conn_state,
    SlePairStateType pair_state, SleDiscReasonType disc_reason)
{
    uint8_t sle_connect_state[] = "sle_dis_connect";

    sample_at_log_print("%s connect state changed callback conn_id:0x%02x, conn_state:0x%x, pair_state:0x%x, "
        "disc_reason:0x%x\r\n", SLE_UART_SERVER_LOG, conn_id, conn_state, pair_state, disc_reason);
    if (addr != NULL) {
        sample_at_log_print("%s connect state changed callback addr:%02x:**:**:**:%02x:%02x\r\n",
            SLE_UART_SERVER_LOG, addr->addr[BT_INDEX_0], addr->addr[BT_INDEX_4], addr->addr[BT_INDEX_5]);
    }

    if (conn_state == OH_SLE_ACB_STATE_CONNECTED) {
        ssap_exchange_info_t parameter = { 0 };
        parameter.mtu_size = SLE_MTU_SIZE_DEFAULT;
        parameter.version = 1;
        g_sle_conn_hdl = conn_id;
        g_sle_connected = true;
        __sync_synchronize();
        (void)ssaps_set_info(g_server_id, &parameter);
    } else if (conn_state == OH_SLE_ACB_STATE_DISCONNECTED) {
        g_sle_conn_hdl = 0;
        g_sle_pair_hdl = 0;
        g_sle_connected = false;
        __sync_synchronize();
        if (g_sle_uart_server_msg_queue != NULL) {
            g_sle_uart_server_msg_queue(sle_connect_state, sizeof(sle_connect_state));
        }
    }
}

static void sle_pair_complete_cbk(uint16_t conn_id, const SleAddr *addr, ErrCodeType status)
{
    sample_at_log_print("%s pair complete conn_id:%02x, status:%x\r\n", SLE_UART_SERVER_LOG, conn_id, status);
    if (addr != NULL) {
        sample_at_log_print("%s pair complete addr:%02x:**:**:**:%02x:%02x\r\n",
            SLE_UART_SERVER_LOG, addr->addr[BT_INDEX_0], addr->addr[BT_INDEX_4], addr->addr[BT_INDEX_5]);
    }
    g_sle_pair_hdl = (uint16_t)(conn_id + 1U);
    __sync_synchronize();
}

static errcode_t sle_conn_register_cbks(void)
{
    ErrCodeType ret;
    SleConnectionCallbacks conn_cbks = { 0 };

    conn_cbks.connectStateChangedCb = sle_connect_state_changed_cbk;
    conn_cbks.pairCompleteCb = sle_pair_complete_cbk;
    ret = SleConnectionRegisterCallbacks(&conn_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_conn_register_cbks,sle_connection_register_callbacks fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}

static void ssaps_read_request_callbacks(uint8_t server_id, uint16_t conn_id, ssaps_req_read_cb_t *read_cb_para,
    errcode_t status)
{
    (void)server_id;
    (void)conn_id;
    (void)read_cb_para;
    (void)status;
}

static void ssaps_write_request_callbacks(uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_cb_para,
    errcode_t status)
{
    (void)server_id;
    (void)conn_id;
    (void)status;

    if (write_cb_para == NULL || write_cb_para->value == NULL || write_cb_para->length == 0U) {
        return;
    }

    write_cb_para->value[write_cb_para->length - 1U] = '\0';
    printf("client_send_data: %s\r\n", write_cb_para->value);
}

errcode_t sle_uart_server_init(void)
{
    errcode_t ret;
    bool enable_ret;

    if (g_sle_init_started) {
        return ERRCODE_SLE_SUCCESS;
    }

    if (g_sle_send_mutex == NULL) {
        g_sle_send_mutex = osMutexNew(NULL);
        if (g_sle_send_mutex == NULL) {
            return ERRCODE_FAIL;
        }
    }

    ret = sle_uart_announce_register_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_uart_server_init,sle_uart_announce_register_cbks fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_conn_register_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_uart_server_init,sle_conn_register_cbks fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_ssaps_register_cbks(ssaps_read_request_callbacks, ssaps_write_request_callbacks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_uart_server_init,sle_ssaps_register_cbks fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }

    g_sle_init_started = true;
    __sync_synchronize();

    enable_ret = EnableSle();
    if (!enable_ret) {
        sample_at_log_print("%s EnableSle returned false, wait callback\r\n", SLE_UART_SERVER_LOG);
    }

    sample_at_log_print("%s init ok\r\n", SLE_UART_SERVER_LOG);
    return ERRCODE_SLE_SUCCESS;
}

errcode_t sle_enable_server_cbk(void)
{
    errcode_t ret;

    ret = sle_uart_server_add();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_uart_server_init,sle_uart_server_add fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_uart_server_adv_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_uart_server_init,sle_uart_server_adv_init fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}

void sle_uart_server_register_msg(sle_uart_server_msg_queue sle_uart_server_msg)
{
    g_sle_uart_server_msg_queue = sle_uart_server_msg;
}

uint16_t sle_uart_client_is_connected(void)
{
    __sync_synchronize();
    return g_sle_connected ? (uint16_t)(g_sle_conn_hdl + 1U) : 0U;
}

int uart_sle_send_data(uint8_t *data, uint8_t length)
{
    return (int)sle_uart_server_send_report_by_handle(data, length);
}

static void SleTask(void *arg)
{
    (void)arg;
    osal_msleep(SLE_SERVER_INIT_DELAY_MS);
    uart_init_config();
    (void)sle_uart_server_init();
}

static void SleServerExample(void)
{
    osThreadAttr_t attr = {
        .name = "SleTask",
        .attr_bits = 0U,
        .cb_mem = NULL,
        .cb_size = 0U,
        .stack_mem = NULL,
        .stack_size = 2048,
        .priority = 25,
    };

    if (osThreadNew(SleTask, NULL, &attr) == NULL) {
        printf("[SleExample] Falied to create SleTask!\n");
    } else {
        printf("[SleExample] create SleTask successfully!\n");
    }
}

SYS_RUN(SleServerExample);
