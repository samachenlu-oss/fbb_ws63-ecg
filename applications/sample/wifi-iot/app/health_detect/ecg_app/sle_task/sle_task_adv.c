/**
 * sle_task server advertising implementation
 *
 * 对齐 sle_uart_demo 的广播模块切分和初始化顺序。
 * 为了兼容按名称扫描的上位机，名称同时放进主广播和扫描响应。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ohos_sle_device_discovery.h"
#include "osal_task.h"
#include "securec.h"
#include "sle_errcode.h"
#include "sle_task.h"
#include "sle_task_adv.h"

/* sle device name */
#define NAME_MAX_LENGTH 16
/* 连接调度间隔12.5ms，单位125us */
#define SLE_CONN_INTV_MIN_DEFAULT                 0x64
/* 连接调度间隔12.5ms，单位125us */
#define SLE_CONN_INTV_MAX_DEFAULT                 0x64
/* 连接调度间隔25ms，单位125us */
#define SLE_ADV_INTERVAL_MIN_DEFAULT              0xC8
/* 连接调度间隔25ms，单位125us */
#define SLE_ADV_INTERVAL_MAX_DEFAULT              0xC8
/* 超时时间5000ms，单位10ms */
#define SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT      0x1F4
/* 超时时间4990ms，单位10ms */
#define SLE_CONN_MAX_LATENCY                      0x1F3
/* 广播发送功率 */
#define SLE_ADV_TX_POWER                          10
/* 广播ID */
#define SLE_ADV_HANDLE_DEFAULT                    1
/* 最大广播数据长度 */
#define SLE_ADV_DATA_LEN_MAX                      251
/* 广播名称 */
static uint8_t sle_local_name[NAME_MAX_LENGTH] = "sle_uart_server";
#define SLE_SERVER_INIT_DELAY_MS                  1000
#define sample_at_log_print(fmt, args...)         printf(fmt, ##args)
#define SLE_UART_SERVER_LOG                       "[sle uart server]"

static uint16_t sle_set_adv_local_name(uint8_t *adv_data, uint16_t max_len)
{
    errno_t ret;
    uint16_t index = 0U;
    uint8_t *local_name = sle_local_name;
    uint8_t local_name_len = (uint8_t)(sizeof(sle_local_name) - 1U);

    if (adv_data == NULL || max_len < (uint16_t)(local_name_len + 2U)) {
        return 0U;
    }

    sample_at_log_print("%s local_name_len = %d\r\n", SLE_UART_SERVER_LOG, local_name_len);
    sample_at_log_print("%s local_name: ", SLE_UART_SERVER_LOG);
    for (uint8_t i = 0U; i < local_name_len; i++) {
        sample_at_log_print("0x%02x ", local_name[i]);
    }
    sample_at_log_print("\r\n");

    adv_data[index++] = (uint8_t)(local_name_len + 1U);
    adv_data[index++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    ret = memcpy_s(&adv_data[index], max_len - index, local_name, local_name_len);
    if (ret != EOK) {
        sample_at_log_print("%s memcpy fail\r\n", SLE_UART_SERVER_LOG);
        return 0U;
    }
    return (uint16_t)(index + local_name_len);
}

static uint16_t sle_set_adv_data(uint8_t *adv_data)
{
    errno_t ret;
    uint16_t idx = 0U;
    uint16_t name_len;
    size_t len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value adv_disc_level = {
        .length = (uint8_t)(len - 1U),
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    struct sle_adv_common_value adv_access_mode = {
        .length = (uint8_t)(len - 1U),
        .type = SLE_ADV_DATA_TYPE_ACCESS_MODE,
        .value = 0U,
    };

    if (adv_data == NULL) {
        return 0U;
    }

    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_disc_level, len);
    if (ret != EOK) {
        sample_at_log_print("%s adv_disc_level memcpy fail\r\n", SLE_UART_SERVER_LOG);
        return 0U;
    }
    idx += (uint16_t)len;

    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_access_mode, len);
    if (ret != EOK) {
        sample_at_log_print("%s adv_access_mode memcpy fail\r\n", SLE_UART_SERVER_LOG);
        return 0U;
    }
    idx += (uint16_t)len;

    name_len = sle_set_adv_local_name(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx);
    if (name_len == 0U) {
        return 0U;
    }
    idx += name_len;

    return idx;
}

static uint16_t sle_set_scan_response_data(uint8_t *scan_rsp_data)
{
    errno_t ret;
    uint16_t idx = 0U;
    uint16_t name_len;
    size_t scan_rsp_data_len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value tx_power_level = {
        .length = (uint8_t)(scan_rsp_data_len - 1U),
        .type = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL,
        .value = SLE_ADV_TX_POWER,
    };

    if (scan_rsp_data == NULL) {
        return 0U;
    }

    ret = memcpy_s(scan_rsp_data, SLE_ADV_DATA_LEN_MAX, &tx_power_level, scan_rsp_data_len);
    if (ret != EOK) {
        sample_at_log_print("%s sle scan response data memcpy fail\r\n", SLE_UART_SERVER_LOG);
        return 0U;
    }
    idx += (uint16_t)scan_rsp_data_len;

    name_len = sle_set_adv_local_name(&scan_rsp_data[idx], SLE_ADV_DATA_LEN_MAX - idx);
    if (name_len == 0U) {
        return 0U;
    }
    idx += name_len;
    return idx;
}

static int sle_set_default_announce_param(void)
{
    errno_t ret;
    ErrCodeType name_ret;
    SleAnnounceParam param = { 0 };
    uint8_t index;
    uint8_t local_addr[OH_SLE_ADDR_LEN] = { 0x78, 0x70, 0x60, 0x88, 0x96, 0x45 };

    param.announceMode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announceHandle = SLE_ADV_HANDLE_DEFAULT;
    param.announceGtRole = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announceLevel = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announceChannelMap = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announceIntervalMin = SLE_ADV_INTERVAL_MIN_DEFAULT;
    param.announceIntervalMax = SLE_ADV_INTERVAL_MAX_DEFAULT;
    param.announce_tx_power = SLE_ADV_TX_POWER;
    param.connIntervalMin = SLE_CONN_INTV_MIN_DEFAULT;
    param.connIntervalMax = SLE_CONN_INTV_MAX_DEFAULT;
    param.connMaxLatency = SLE_CONN_MAX_LATENCY;
    param.connSupervisionTimeout = SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT;
    param.ownAddr.type = 0;
    ret = memcpy_s(param.ownAddr.addr, OH_SLE_ADDR_LEN, local_addr, OH_SLE_ADDR_LEN);

    if (ret != EOK) {
        sample_at_log_print("%s sle_set_default_announce_param data memcpy fail\r\n", SLE_UART_SERVER_LOG);
        return ERRCODE_MEMCPY;
    }

    name_ret = SleSetLocalName(sle_local_name, (uint8_t)(sizeof(sle_local_name) - 1U));
    if (name_ret != 0) {
        sample_at_log_print("%s SleSetLocalName fail:%x\r\n", SLE_UART_SERVER_LOG, name_ret);
        return name_ret;
    }

    sample_at_log_print("%s sle_uart_local addr: ", SLE_UART_SERVER_LOG);
    for (index = 0U; index < OH_SLE_ADDR_LEN; index++) {
        sample_at_log_print("0x%02x ", param.ownAddr.addr[index]);
    }
    sample_at_log_print("\r\n");
    return SleSetAnnounceParam(param.announceHandle, &param);
}

static int sle_set_default_announce_data(void)
{
    errcode_t ret;
    uint16_t announce_data_len = 0U;
    uint16_t seek_data_len = 0U;
    SleAnnounceData data = { 0 };
    uint8_t adv_handle = SLE_ADV_HANDLE_DEFAULT;
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX] = { 0 };
    uint8_t seek_rsp_data[SLE_ADV_DATA_LEN_MAX] = { 0 };
    uint16_t data_index = 0U;

    announce_data_len = sle_set_adv_data(announce_data);
    if (announce_data_len == 0U) {
        return ERRCODE_SLE_FAIL;
    }
    data.announceData = announce_data;
    data.announceDataLen = announce_data_len;

    sample_at_log_print("%s data.announce_data_len = %d\r\n", SLE_UART_SERVER_LOG, data.announceDataLen);
    sample_at_log_print("%s data.announce_data: ", SLE_UART_SERVER_LOG);
    for (data_index = 0U; data_index < data.announceDataLen; data_index++) {
        sample_at_log_print("0x%02x ", data.announceData[data_index]);
    }
    sample_at_log_print("\r\n");

    seek_data_len = sle_set_scan_response_data(seek_rsp_data);
    if (seek_data_len == 0U) {
        return ERRCODE_SLE_FAIL;
    }
    data.seekRspData = seek_rsp_data;
    data.seekRspDataLen = seek_data_len;

    sample_at_log_print("%s data.seek_rsp_data_len = %d\r\n", SLE_UART_SERVER_LOG, data.seekRspDataLen);
    sample_at_log_print("%s data.seek_rsp_data: ", SLE_UART_SERVER_LOG);
    for (data_index = 0U; data_index < data.seekRspDataLen; data_index++) {
        sample_at_log_print("0x%02x ", data.seekRspData[data_index]);
    }
    sample_at_log_print("\r\n");

    ret = SleSetAnnounceData(adv_handle, &data);
    if (ret == ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s set announce data success.\r\n", SLE_UART_SERVER_LOG);
    } else {
        sample_at_log_print("%s set adv param fail.\r\n", SLE_UART_SERVER_LOG);
        return ret;
    }
    return ret;
}

static void sle_announce_enable_cbk(uint32_t announce_id, ErrCodeType status)
{
    sample_at_log_print("%s sle announce enable callback id:%02x, state:%x\r\n",
        SLE_UART_SERVER_LOG, announce_id, status);
}

static void sle_announce_disable_cbk(uint32_t announce_id, ErrCodeType status)
{
    sample_at_log_print("%s sle announce disable callback id:%02x, state:%x\r\n",
        SLE_UART_SERVER_LOG, announce_id, status);
}

static void sle_announce_terminal_cbk(uint32_t announce_id)
{
    sample_at_log_print("%s sle announce terminal callback id:%02x\r\n", SLE_UART_SERVER_LOG, announce_id);
}

static void sle_enable_cbk(ErrCodeType status)
{
    errcode_t ret;

    sample_at_log_print("%s sle enable callback status:%x\r\n", SLE_UART_SERVER_LOG, status);
    if (status != 0) {
        return;
    }
    osal_msleep(SLE_SERVER_INIT_DELAY_MS);
    ret = sle_enable_server_cbk();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_enable_server_cbk fail:%x\r\n", SLE_UART_SERVER_LOG, ret);
    }
}

errcode_t sle_uart_announce_register_cbks(void)
{
    ErrCodeType ret;
    SleAnnounceSeekCallbacks seek_cbks = { 0 };

    seek_cbks.sleAnnounceEnableCb = sle_announce_enable_cbk;
    seek_cbks.sleAnnounceDisableCb = sle_announce_disable_cbk;
    seek_cbks.sleAnnounceTerminalCb = sle_announce_terminal_cbk;
    seek_cbks.sleEnableCb = sle_enable_cbk;
    ret = SleAnnounceSeekRegisterCallbacks(&seek_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_uart_announce_register_cbks,register_callbacks fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}

errcode_t sle_uart_server_adv_init(void)
{
    errcode_t ret;

    ret = (errcode_t)sle_set_default_announce_param();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_set_default_announce_param fail:%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ret;
    }

    ret = (errcode_t)sle_set_default_announce_data();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_set_default_announce_data fail:%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ret;
    }

    ret = SleStartAnnounce(SLE_ADV_HANDLE_DEFAULT);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s sle_uart_server_adv_init,sle_start_announce fail:%x\r\n",
            SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}
