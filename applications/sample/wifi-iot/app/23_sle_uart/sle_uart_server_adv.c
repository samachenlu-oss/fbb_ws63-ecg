/**
# Copyright (C) 2024 HiHope Open Source Organization .
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
 */
#include "securec.h"
#include "errcode.h"
#include "osal_addr.h"
#include "sle_common.h"
#include "sle_uart_server.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "osal_debug.h"
#include "osal_task.h"
#include "string.h"
#include "sle_uart_server_adv.h"

#include "ohos_sle_common.h"
#include "ohos_sle_errcode.h"
#include "ohos_sle_ssap_server.h"
#include "ohos_sle_ssap_client.h"
#include "ohos_sle_device_discovery.h"
#include "ohos_sle_connection_manager.h"

/*
 * ========================= 手机扫描兼容性调试修改开始 =========================
 *
 * 下面这几个改动点不是“通用示例配置”，而是专门为了排查
 * “开发板能和另一块 WS63 互连，但华为手机 APP 扫不到/连不上”这个问题做的收缩化调整。
 *
 * 当前策略是：
 * 1. 先把广播正文压缩到最小。
 * 2. 先验证手机能否识别设备。
 * 3. 验证通过后，再逐项把 UUID / Access Mode / 完整名字等字段加回去。
 *
 * 因此，后续如果你准备恢复为量产广播格式，必须重新审视这里的每一处“最小化”处理。
 * ========================== 手机扫描兼容性调试修改结束 ==========================
 */
/*
 * 设备名长度按“实际字符数 + 结尾 '\0'”设置。
 * 当前名称为 "abc"，共 3 个可见字符，因此数组长度取 4。
 * 这里不要再写成一个过大的固定值，否则后续如果又回到 sizeof(array) 的写法，
 * 很容易把无效的 0x00 一起带进广播名字字段。
 */
#define NAME_MAX_LENGTH 4U
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
#define SLE_ADV_TX_POWER  10
/* 广播ID */
#define SLE_ADV_HANDLE_DEFAULT                    1
/* 最大广播数据长度 */
#define SLE_ADV_DATA_LEN_MAX                      251
/*
 * 当前阶段先把设备名收缩为一个非常短的名字，目的是尽量贴近手机可识别的成功样本，
 * 降低广播正文中的变量数量，优先验证“手机能否扫到设备”。
 *
 * 这里如果你后面要恢复正式名称，不能只改这个字符串。
 * 还必须同步检查：
 * 1. NAME_MAX_LENGTH 是否仍然足够。
 * 2. 串口打印出来的广播帧长度是否符合预期。
 * 3. 手机 APP 的扫描过滤规则是否允许更长的名称。
 */
static uint8_t g_sleLocalName[NAME_MAX_LENGTH] = "abc";
#define SLE_SERVER_INIT_DELAY_MS    1000
#define printf(fmt, args...) osal_printk(fmt, ##args)
#define SLE_UART_SERVER_LOG "[sle uart server]"

/*
 * 组装“设备名称”字段。
 *
 * 这里有两个关键点：
 * 1. 名称长度使用 strlen 动态计算，避免把数组尾部的 '\0' 或多余 0x00 带入广播。
 * 2. 名称类型使用 SLE_ADV_DATA_TYPE_SHORTENED_LOCAL_NAME(0x0A)，
 *    是为了尽量贴近你给出的那条手机可连接成功的广播样本。
 *
 * 当前生成的字段格式为：
 *   [Type=0x0A][Len=名称长度][Value=名称ASCII]
 *
 * 以 "abc" 为例，最终写入的字节就是：
 *   0A 03 61 62 63
 */
static uint16_t SleSetAdvLocalName(uint8_t *advData, uint16_t maxLen)
{
    errno_t ret;
    uint8_t index = 0;

    uint8_t *localName = g_sleLocalName;
    uint8_t localNameLen;

    if (advData == NULL) {
        printf("%s invalid local name buffer\r\n", SLE_UART_SERVER_LOG);
        return 0;
    }

    localNameLen = (uint8_t)strlen((const char *)localName);
    if (localNameLen == 0U || maxLen < (uint16_t)(localNameLen + 2U)) {
        printf("%s invalid local name length:%u\r\n", SLE_UART_SERVER_LOG, localNameLen);
        return 0;
    }

    printf("%s localNameLen = %d\r\n", SLE_UART_SERVER_LOG, localNameLen);
    printf("%s local_name: ", SLE_UART_SERVER_LOG);
    for (uint8_t i = 0; i < localNameLen; i++) {
        printf("0x%02x ", localName[i]);
    }
    printf("\r\n");
    /*
     * 写入“缩写设备名”字段类型。
     * 这里刻意不用 COMPLETE_LOCAL_NAME(0x0B)，
     * 是因为当前目标不是追求字段完整，而是优先逼近那条手机可识别的成功样本。
     */
    advData[index++] = SLE_ADV_DATA_TYPE_SHORTENED_LOCAL_NAME;
    /*
     * 紧跟其后的长度只统计名字有效载荷本身，
     * 不包含当前 type 字节，也不包含当前 len 字节。
     */
    advData[index++] = localNameLen;
    ret = memcpy_s(&advData[index], maxLen - index, localName, localNameLen);
    if (ret != EOK) {
        printf("%s memcpy fail\r\n", SLE_UART_SERVER_LOG);
        return 0;
    }
    return (uint16_t)index + localNameLen;
}

/*
 * 组装主广播帧（announce data）。
 *
 * 这里故意不再像之前那样继续塞 Access Mode、UUID、完整名称等字段，
 * 而是先压缩成一个“最小可识别”广播：
 *
 *   [Type=0x01][Len=0x01][Value=0x01]
 *   [Type=0x0A][Len=0x03][Value='a''b''c']
 *
 * 也就是：
 *   01 01 01 0A 03 61 62 63
 *
 * 这么做的目的不是最终量产格式，而是先验证：
 * “华为手机 APP 是否能识别这种与成功样本更接近的最小广播正文”。
 *
 * 如果这一步能扫到设备，再逐步把 UUID、Access Mode 等字段重新加回来；
 * 如果这一步仍扫不到，就说明问题不在这些附加字段，而在更底层的兼容性或协议解析。
 */
static uint16_t SleSetAdvData(uint8_t *advData)
{
    uint16_t idx = 0;
    uint16_t nameLen = 0;

    if (advData == NULL || SLE_ADV_DATA_LEN_MAX < 3U) {
        printf("%s invalid adv buffer\r\n", SLE_UART_SERVER_LOG);
        return 0;
    }

    /*
     * Discovery Level 字段：
     * 0x01 0x01 0x01
     * 含义：类型=发现等级，长度=1，值=普通可发现。
     */
    advData[idx++] = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL;
    advData[idx++] = 1U;
    advData[idx++] = (uint8_t)SLE_ANNOUNCE_LEVEL_NORMAL;

    nameLen = SleSetAdvLocalName(&advData[idx], SLE_ADV_DATA_LEN_MAX - idx);
    if (nameLen == 0) {
        return 0;
    }
    idx += nameLen;

    return idx;
}

/*
 * 组装扫描响应帧（scan response data）。
 *
 * 当前也刻意收缩为最小内容，只保留发射功率：
 *   [Type=0x0C][Len=0x01][Value=0x0A]
 * 即：
 *   0C 01 0A
 *
 * 这样做是为了避免把设备名重复塞进 scan response 后，
 * 又给手机侧解析引入额外变量。
 */
static uint16_t SleSetScanResponseData(uint8_t *scanRspData)
{
    uint16_t idx = 0;
    if (scanRspData == NULL || SLE_ADV_DATA_LEN_MAX < 3U) {
        printf("%s invalid scan response buffer\r\n", SLE_UART_SERVER_LOG);
        return 0;
    }

    /* TX Power 字段：类型=发射功率，长度=1，值=10 */
    scanRspData[idx++] = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL;
    scanRspData[idx++] = 1U;
    scanRspData[idx++] = (uint8_t)SLE_ADV_TX_POWER;
    return idx;
}

static int SleSetDefaultAnnounceParam(void)
{
    errno_t ret;
    ErrCodeType nameRet;
    SleAnnounceParam param = {0};
    uint8_t index;
    uint8_t localNameLen;
    unsigned char local_addr[SLE_ADDR_LEN] = { 0x78, 0x70, 0x60, 0x88, 0x96, 0x45 };
    param.announceMode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announceHandle = SLE_ADV_HANDLE_DEFAULT;
    param.announceGtRole = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announceLevel = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announceChannelMap = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announceIntervalMin = SLE_ADV_INTERVAL_MIN_DEFAULT;
    param.announceIntervalMax = SLE_ADV_INTERVAL_MAX_DEFAULT;
    param.connIntervalMin = SLE_CONN_INTV_MIN_DEFAULT;
    param.connIntervalMax = SLE_CONN_INTV_MAX_DEFAULT;
    param.connMaxLatency = SLE_CONN_MAX_LATENCY;
    param.connSupervisionTimeout = SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT;
    param.ownAddr.type = 0;
    ret = memcpy_s(param.ownAddr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN);
    if (ret != EOK) {
        printf("%s SleSetDefaultAnnounceParam data memcpy fail\r\n", SLE_UART_SERVER_LOG);
        return 0;
    }

    /*
     * 把本地设备名同步注册给协议栈。
     * 注意这里必须和广播正文里使用的名字保持一致，否则会出现：
     * 1. 串口打印的广播名字是一套
     * 2. 协议栈内部记录的本地名字又是另一套
     *
     * 这里同样使用 strlen，而不是 sizeof(array)-1，
     * 避免名字字段再次被无效尾字节污染。
     */
    localNameLen = (uint8_t)strlen((const char *)g_sleLocalName);
    if (localNameLen == 0U) {
        printf("%s invalid local name\r\n", SLE_UART_SERVER_LOG);
        return ERRCODE_INVALID_PARAM;
    }

    nameRet = SleSetLocalName(g_sleLocalName, localNameLen);
    if (nameRet != ERRCODE_SLE_SUCCESS) {
        printf("%s SleSetLocalName fail:%x\r\n", SLE_UART_SERVER_LOG, nameRet);
        return nameRet;
    }

    printf("%s sle_uart_local addr: ", SLE_UART_SERVER_LOG);
    for (index = 0; index < SLE_ADDR_LEN; index++) {
        printf("0x%02x ", param.ownAddr.addr[index]);
    }
    printf("\r\n");
    return SleSetAnnounceParam(param.announceHandle, &param);
}

static int SleSetDefaultAnnounceData(void)
{
    errcode_t ret;
    uint8_t announceDataLen = 0;
    uint8_t seekDataLen = 0;
    SleAnnounceData data = {0};
    uint8_t advHandle = SLE_ADV_HANDLE_DEFAULT;
    uint8_t announceData[SLE_ADV_DATA_LEN_MAX] = {0};
    uint8_t seekRspData[SLE_ADV_DATA_LEN_MAX] = {0};
    uint8_t dataIndex = 0;

    /*
     * 先组装主广播帧。
     * 这个 announceData 就是手机主动扫描时最先看到的正文，
     * 也是当前排查“为何手机扫不到”的核心观测对象。
     */
    announceDataLen = SleSetAdvData(announceData);
    if (announceDataLen == 0) {
        return ERRCODE_SLE_FAIL;
    }
    data.announceData = announceData;
    data.announceDataLen = announceDataLen;

    /*
     * 这里的串口打印非常关键。
     * 你后面每次改广播格式，都应该先看这里打印出来的十六进制帧，
     * 再判断问题到底出在：
     * 1. 代码拼帧错误；
     * 2. 协议栈二次处理；
     * 3. 手机 APP 过滤规则不匹配。
     */
    printf("%s data.announce_data_len = %d\r\n", SLE_UART_SERVER_LOG, data.announceDataLen);
    printf("%s data.announce_data: ", SLE_UART_SERVER_LOG);
    for (dataIndex = 0; dataIndex<data.announceDataLen; dataIndex++) {
        printf("0x%02x ", data.announceData[dataIndex]);
    }
    printf("\r\n");

    /*
     * 再组装扫描响应帧。
     * 当前这里也故意只保留最小字段，避免 scan response 里再引入名字、UUID 等额外变量。
     */
    seekDataLen = SleSetScanResponseData(seekRspData);
    if (seekDataLen == 0) {
        return ERRCODE_SLE_FAIL;
    }
    data.seekRspData = seekRspData;
    data.seekRspDataLen = seekDataLen;

    printf("%s data.seek_rsp_data_len = %d\r\n", SLE_UART_SERVER_LOG, data.seekRspDataLen);
    printf("%s data.seek_rsp_data: ", SLE_UART_SERVER_LOG);
    for (dataIndex = 0; dataIndex<data.seekRspDataLen; dataIndex++) {
        printf("0x%02x ", data.seekRspData[dataIndex]);
    }
    printf("\r\n");

    /*
     * 把“主广播帧 + 扫描响应帧”一次性下发给协议栈。
     * 如果这里成功，但手机仍然扫不到，问题通常就不再是“没有调用配置接口”，
     * 而更可能是广播内容格式、手机侧过滤条件、或者底层协议兼容性。
     */
    ret = SleSetAnnounceData(advHandle, &data);
    if (ret == ERRCODE_SLE_SUCCESS) {
        printf("%s set announce data success.\r\n", SLE_UART_SERVER_LOG);
    } else {
        printf("%s set adv param fail.\r\n", SLE_UART_SERVER_LOG);
        return ret;
    }
    return ret;
}

static void sle_announce_enable_cbk(uint32_t announceId, errcode_t status)
{
    printf("%s sle announce enable callback id:%02x, state:%x\r\n", SLE_UART_SERVER_LOG, announceId,
        status);
}

static void sle_announce_disable_cbk(uint32_t announceId, errcode_t status)
{
    printf("%s sle announce disable callback id:%02x, state:%x\r\n", SLE_UART_SERVER_LOG, announceId,
        status);
}

static void SleAnnounceTerminalCbk(uint32_t announceId)
{
    printf("%s sle announce terminal callback id:%02x\r\n", SLE_UART_SERVER_LOG, announceId);
}

static void sle_enable_cbk(errcode_t status)
{
    printf("%s sle enable callback status:%x\r\n", SLE_UART_SERVER_LOG, status);
    if (status != ERRCODE_SLE_SUCCESS) {
        return;
    }
    osal_msleep(SLE_SERVER_INIT_DELAY_MS);
    (void)sle_enable_server_cbk();
}

errcode_t sle_uart_announce_register_cbks(void)
{
    errcode_t ret;
    SleAnnounceSeekCallbacks seek_cbks = {0};
    seek_cbks.sleAnnounceEnableCb = sle_announce_enable_cbk;
    seek_cbks.sleAnnounceDisableCb = sle_announce_disable_cbk;
    seek_cbks.sleAnnounceTerminalCb = SleAnnounceTerminalCbk;
    seek_cbks.sleEnableCb = sle_enable_cbk;
    ret = SleAnnounceSeekRegisterCallbacks(&seek_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s sle_uart_announce_register_cbks,register_callbacks fail :%x\r\n",
        SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}

errcode_t sle_uart_server_adv_init(void)
{
    errcode_t ret;
    SleAddr addr;
    unsigned char local_addr[SLE_ADDR_LEN] = { 0x78, 0x70, 0x60, 0x88, 0x96, 0x45 };
    addr.type = 0;
    if (memcpy_s(addr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN) != EOK) {
        return ERRCODE_MEMCPY;
    }
    ret = SleSetLocalAddr(&addr);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s SleSetLocalAddr fail :%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    ret = SleSetDefaultAnnounceParam();
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s SleSetDefaultAnnounceParam fail :%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    ret = SleSetDefaultAnnounceData();
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s SleSetDefaultAnnounceData fail :%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    ret = SleStartAnnounce(SLE_ADV_HANDLE_DEFAULT);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s sle_uart_server_adv_init,sle_start_announce fail :%x\r\n", SLE_UART_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}
