/*
 * ECG SLE Simulator - 第一版
 *
 * 这一版严格按你补充后的链路来搭：
 *
 * 1. 模拟“串口解包后的结果”，也就是：
 *    module_id + payload_type + payload
 *
 * 2. 生产者线程持续往输入队列里塞两类数据：
 *    - 500Hz ECG 波形点：MODULE_ECG + DAT_ECG_WAVE + payload
 *    - 1Hz 心率数据：MODULE_ECG + DAT_ECG_HR + payload
 *
 * 3. 聚合线程从输入队列取数据：
 *    - ECG 点先积攒
 *    - 积满指定数量后，重新打包成“逻辑 ECG 包”
 *    - 心率则单独打成“逻辑 HR 包”
 *    - 然后把这些“待发送包”放入 SLE 发送队列
 *
 * 4. SLE 线程只做一件事：
 *    看发送队列里有没有包，有就发，没有就阻塞等待。
 *
 * -------------------------
 * 关于“500Hz”和“200点一包”的关系
 * -------------------------
 * 500Hz = 每 2ms 产生 1 个点。
 * 所以：
 * - 100 个点 = 200ms
 * - 200 个点 = 400ms
 *
 * 你刚才的描述里同时出现了“每 200ms 发送一次”和“积攒 200 个心电数据”，
 * 这两个条件在 500Hz 下是矛盾的。
 *
 * 第一版这里我优先按“积攒 200 个心电数据”实现，
 * 也就是 ECG 逻辑包大约每 400ms 生成一次。
 *
 * 如果你后面确认必须每 200ms 发一包，
 * 那只需要把 ECG_SIM_WAVE_BATCH_COUNT 从 200 改成 100 即可。
 */

#include <stdint.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "errcode.h"
#include "ohos_init.h"
#include "osal_task.h"
#include "package.h"
#include "securec.h"
#include "sle_task.h"

/* =========================
 * 1. 业务可调参数
 * ========================= */

/* 模拟的 ECG 原始点采样率：500Hz。 */
#define ECG_SIM_SAMPLE_RATE_HZ 500U

/*
 * 生产者线程每 10ms 运行一次，但每次会连续生成 5 个 ECG 点，
 * 所以平均速率仍然是 500Hz。
 */
#define ECG_SIM_PRODUCER_PERIOD_MS 10U

/*
 * 500Hz * 10ms = 5 点/生产周期。
 * 这样比每 2ms 唤醒一次线程更稳，调度抖动也更小。
 */
#define ECG_SIM_SAMPLES_PER_PRODUCER_TICK ((ECG_SIM_SAMPLE_RATE_HZ * ECG_SIM_PRODUCER_PERIOD_MS) / 1000U)

/*
 * ECG 波形逻辑包里累计多少个 payload。
 * 当前按你的“积攒 200 个心电数据”要求实现。
 *
 * 注意：
 * 500Hz 下 200 个点对应 400ms。
 * 如果你后面要改成“每 200ms 发一包”，把这里改成 100 即可。
 */
#define ECG_SIM_WAVE_BATCH_COUNT 200U

#define ECG_SIM_DEFAULT_HR_BPM 72U

/* 线程参数。 */
#define ECG_SIM_THREAD_STACK_SIZE 0x1200
#define ECG_SIM_THREAD_PRIORITY osPriorityNormal
#define ECG_SIM_STARTUP_DELAY_MS 1500U

/* 输入队列和发送队列大小，必须是 2 的整数次幂，便于位运算取模。 */
#define ECG_SIM_INPUT_QUEUE_SIZE 1024U
#define ECG_SIM_INPUT_QUEUE_MASK (ECG_SIM_INPUT_QUEUE_SIZE - 1U)

#define ECG_SIM_TX_QUEUE_SIZE 32U
#define ECG_SIM_TX_QUEUE_MASK (ECG_SIM_TX_QUEUE_SIZE - 1U)

/*
 * 单个 SLE 分片里允许携带的“业务 payload”最大字节数。
 *
 * 为什么要分片：
 * 当前 uart_task 里解出来的 payload 是 uint32_t。
 * 如果 200 个 ECG 点一起打包，光 payload 就是 200 * 4 = 800 字节，
 * 单个 notify 包放不下，所以必须做应用层分片。
 */
#define ECG_SIM_SLE_FRAGMENT_PAYLOAD_MAX 200U

/* 外层 SLE 发送帧固定开销。 */
#define ECG_SIM_SLE_SYNC0 0x5AU
#define ECG_SIM_SLE_SYNC1 0xA5U
#define ECG_SIM_SLE_PROTOCOL_VERSION 0x01U
#define ECG_SIM_TX_FRAME_FIXED_HEADER_LEN 11U
#define ECG_SIM_TX_FRAME_CHECKSUM_LEN 2U
#define ECG_SIM_TX_FRAME_MAX_LEN \
    (ECG_SIM_TX_FRAME_FIXED_HEADER_LEN + ECG_SIM_SLE_FRAGMENT_PAYLOAD_MAX + ECG_SIM_TX_FRAME_CHECKSUM_LEN)

/* 生产者日志节奏。 */
#define ECG_SIM_STATUS_LOG_INTERVAL 100U

#if (ECG_SIM_INPUT_QUEUE_SIZE & (ECG_SIM_INPUT_QUEUE_SIZE - 1U)) != 0
#error "ECG_SIM_INPUT_QUEUE_SIZE must be power of two."
#endif

#if (ECG_SIM_TX_QUEUE_SIZE & (ECG_SIM_TX_QUEUE_SIZE - 1U)) != 0
#error "ECG_SIM_TX_QUEUE_SIZE must be power of two."
#endif

#if ((ECG_SIM_SAMPLE_RATE_HZ * ECG_SIM_PRODUCER_PERIOD_MS) % 1000U) != 0
#error "SAMPLE_RATE * PRODUCER_PERIOD must be divisible by 1000."
#endif

typedef struct {
    uint8_t module_id;
    uint8_t payload_type;
    uint32_t payload;
} EcgSimInputItem;

typedef struct {
    uint16_t len;
    uint8_t bytes[ECG_SIM_TX_FRAME_MAX_LEN];
} EcgSimTxPacket;

typedef struct {
    EcgSimInputItem buffer[ECG_SIM_INPUT_QUEUE_SIZE];
    volatile uint32_t wr_idx;
    volatile uint32_t rd_idx;
} EcgSimInputQueue;

typedef struct {
    EcgSimTxPacket buffer[ECG_SIM_TX_QUEUE_SIZE];
    volatile uint32_t wr_idx;
    volatile uint32_t rd_idx;
} EcgSimTxQueue;

typedef struct {
    uint32_t generated_ecg_count;
    uint32_t generated_hr_count;
    uint32_t input_drop_count;
    uint32_t tx_drop_count;
    uint32_t sent_packet_count;
    uint32_t logical_wave_seq;
    uint32_t logical_hr_seq;
    uint16_t beat_sample_index;
    uint16_t beat_sample_count;
    uint32_t noise_lcg;
    uint32_t wave_cache_count;
    uint32_t wave_cache[ECG_SIM_WAVE_BATCH_COUNT];
} EcgSimContext;

static EcgSimInputQueue g_ecgSimInputQueue = {0};
static EcgSimTxQueue g_ecgSimTxQueue = {0};
static osSemaphoreId_t g_ecgSimInputSem = NULL;
static osSemaphoreId_t g_ecgSimTxSem = NULL;
static EcgSimContext g_ecgSimContext = {0};

static uint16_t MsToTicksCeil(uint32_t ms)
{
    uint32_t tick_freq = osKernelGetTickFreq();

    if (tick_freq == 0U) {
        return 0U;
    }

    return (uint16_t)((((uint64_t)ms * tick_freq) + 999ULL) / 1000ULL);
}

static void WriteLe16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void WriteLe32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint16_t Checksum16(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0U;
    uint16_t i;

    for (i = 0U; i < len; i++) {
        sum += data[i];
    }

    return (uint16_t)(sum & 0xFFFFU);
}

static int32_t TrianglePulse(uint16_t phase_permille, uint16_t start, uint16_t peak, uint16_t end, int32_t amplitude)
{
    if (start >= peak || peak >= end) {
        return 0;
    }

    if (phase_permille < start || phase_permille > end) {
        return 0;
    }

    if (phase_permille <= peak) {
        return (amplitude * (int32_t)(phase_permille - start)) / (int32_t)(peak - start);
    }

    return (amplitude * (int32_t)(end - phase_permille)) / (int32_t)(end - peak);
}

static int32_t BaselineWander(uint32_t sample_index)
{
    const uint32_t period_samples = ECG_SIM_SAMPLE_RATE_HZ * 4U;
    const uint32_t half_period = period_samples / 2U;
    const int32_t amplitude = 20;
    uint32_t phase = sample_index % period_samples;

    if (phase < half_period) {
        return -amplitude + (int32_t)((2U * (uint32_t)amplitude * phase) / half_period);
    }

    phase -= half_period;
    return amplitude - (int32_t)((2U * (uint32_t)amplitude * phase) / half_period);
}

static int32_t NextNoise(EcgSimContext *ctx)
{
    ctx->noise_lcg = ctx->noise_lcg * 1664525U + 1013904223U;
    return (int32_t)((ctx->noise_lcg >> 24) & 0x1FU) - 15;
}

static uint32_t EcgSimGenerateWavePayload(EcgSimContext *ctx)
{
    /*
     * 这里生成的是“模拟 ADC 原始值”，所以用无符号值返回，
     * 便于直接放进 uart_task 现有的 uint32_t payload 管道里。
     */
    const int32_t adc_base = 2048;
    uint16_t phase_permille;
    int32_t waveform = 0;
    int32_t adc_value;

    if (ctx->beat_sample_count == 0U) {
        ctx->beat_sample_count = 1U;
    }

    phase_permille = (uint16_t)(((uint32_t)ctx->beat_sample_index * 1000U) / ctx->beat_sample_count);

    waveform += TrianglePulse(phase_permille, 90U, 120U, 170U, 70);
    waveform += TrianglePulse(phase_permille, 360U, 395U, 420U, -120);
    waveform += TrianglePulse(phase_permille, 395U, 410U, 440U, 1200);
    waveform += TrianglePulse(phase_permille, 440U, 470U, 520U, -280);
    waveform += TrianglePulse(phase_permille, 620U, 720U, 860U, 240);

    adc_value = adc_base + waveform + BaselineWander(ctx->generated_ecg_count) + NextNoise(ctx);

    if (adc_value < 0) {
        adc_value = 0;
    }
    if (adc_value > 4095) {
        adc_value = 4095;
    }

    ctx->beat_sample_index++;
    if (ctx->beat_sample_index >= ctx->beat_sample_count) {
        ctx->beat_sample_index = 0U;
    }

    ctx->generated_ecg_count++;
    return (uint32_t)adc_value;
}

static int EcgSimInputQueuePush(const EcgSimInputItem *item)
{
    uint32_t current_wr = g_ecgSimInputQueue.wr_idx;
    uint32_t current_rd = g_ecgSimInputQueue.rd_idx;
    uint32_t used = current_wr - current_rd;
    uint32_t write_pos;

    if (used >= ECG_SIM_INPUT_QUEUE_SIZE) {
        return 0;
    }

    write_pos = current_wr & ECG_SIM_INPUT_QUEUE_MASK;
    g_ecgSimInputQueue.buffer[write_pos] = *item;
    __sync_synchronize();
    g_ecgSimInputQueue.wr_idx = current_wr + 1U;
    osSemaphoreRelease(g_ecgSimInputSem);
    return 1;
}

static int EcgSimInputQueuePop(EcgSimInputItem *item)
{
    uint32_t current_rd = g_ecgSimInputQueue.rd_idx;
    uint32_t current_wr = g_ecgSimInputQueue.wr_idx;
    uint32_t read_pos;

    if (current_wr == current_rd) {
        return 0;
    }

    read_pos = current_rd & ECG_SIM_INPUT_QUEUE_MASK;
    *item = g_ecgSimInputQueue.buffer[read_pos];
    __sync_synchronize();
    g_ecgSimInputQueue.rd_idx = current_rd + 1U;
    return 1;
}

static int EcgSimTxQueuePush(const EcgSimTxPacket *packet)
{
    uint32_t current_wr = g_ecgSimTxQueue.wr_idx;
    uint32_t current_rd = g_ecgSimTxQueue.rd_idx;
    uint32_t used = current_wr - current_rd;
    uint32_t write_pos;

    if (used >= ECG_SIM_TX_QUEUE_SIZE) {
        return 0;
    }

    write_pos = current_wr & ECG_SIM_TX_QUEUE_MASK;
    g_ecgSimTxQueue.buffer[write_pos] = *packet;
    __sync_synchronize();
    g_ecgSimTxQueue.wr_idx = current_wr + 1U;
    osSemaphoreRelease(g_ecgSimTxSem);
    return 1;
}

static int EcgSimTxQueuePeek(EcgSimTxPacket *packet)
{
    uint32_t current_rd = g_ecgSimTxQueue.rd_idx;
    uint32_t current_wr = g_ecgSimTxQueue.wr_idx;
    uint32_t read_pos;

    if (current_wr == current_rd) {
        return 0;
    }

    read_pos = current_rd & ECG_SIM_TX_QUEUE_MASK;
    *packet = g_ecgSimTxQueue.buffer[read_pos];
    return 1;
}

static void EcgSimTxQueueConsume(void)
{
    uint32_t current_rd = g_ecgSimTxQueue.rd_idx;
    __sync_synchronize();
    g_ecgSimTxQueue.rd_idx = current_rd + 1U;
}

static uint16_t EcgSimBuildTxFrame(uint8_t module_id, uint8_t payload_type, uint16_t logical_seq,
                                   uint8_t fragment_index, uint8_t fragment_total, const uint8_t *payload,
                                   uint16_t payload_len, uint8_t *out_buf, uint16_t out_buf_size)
{
    uint16_t offset = 0U;
    uint16_t checksum;

    if (out_buf == NULL || payload == NULL) {
        return 0U;
    }

    if (out_buf_size < (ECG_SIM_TX_FRAME_FIXED_HEADER_LEN + payload_len + ECG_SIM_TX_FRAME_CHECKSUM_LEN)) {
        return 0U;
    }

    /*
     * 外层帧就是“通过 SLE 发给华为设备的最终格式”。
     * 我把 module_id 和 payload_type 放到了固定头里，方便上位机直接按业务类型分发。
     */
    out_buf[offset++] = ECG_SIM_SLE_SYNC0;
    out_buf[offset++] = ECG_SIM_SLE_SYNC1;
    out_buf[offset++] = ECG_SIM_SLE_PROTOCOL_VERSION;
    out_buf[offset++] = module_id;
    out_buf[offset++] = payload_type;
    WriteLe16(&out_buf[offset], logical_seq);
    offset += 2U;
    out_buf[offset++] = fragment_index;
    out_buf[offset++] = fragment_total;
    WriteLe16(&out_buf[offset], payload_len);
    offset += 2U;

    if (memcpy_s(&out_buf[offset], out_buf_size - offset, payload, payload_len) != EOK) {
        return 0U;
    }
    offset += payload_len;

    checksum = Checksum16(out_buf, offset);
    WriteLe16(&out_buf[offset], checksum);
    offset += 2U;

    return offset;
}

static void EcgSimEnqueueLogicalPayload(uint8_t module_id, uint8_t payload_type, uint16_t logical_seq,
                                        const uint8_t *payload, uint16_t payload_len)
{
    uint16_t fragment_total;
    uint16_t fragment_index;

    /*
     * 一个逻辑 payload 可能很大，例如：
     * ECG 200 点 * 4 字节 = 800 字节。
     * 所以这里把逻辑 payload 切成多个 SLE 分片，逐个入发送队列。
     */
    fragment_total = (payload_len + ECG_SIM_SLE_FRAGMENT_PAYLOAD_MAX - 1U) / ECG_SIM_SLE_FRAGMENT_PAYLOAD_MAX;
    if (fragment_total == 0U) {
        fragment_total = 1U;
    }

    for (fragment_index = 0U; fragment_index < fragment_total; fragment_index++) {
        EcgSimTxPacket packet = {0};
        uint16_t chunk_offset = fragment_index * ECG_SIM_SLE_FRAGMENT_PAYLOAD_MAX;
        uint16_t remain = payload_len - chunk_offset;
        uint16_t chunk_len = (remain > ECG_SIM_SLE_FRAGMENT_PAYLOAD_MAX) ? ECG_SIM_SLE_FRAGMENT_PAYLOAD_MAX : remain;

        packet.len = EcgSimBuildTxFrame(module_id, payload_type, logical_seq, (uint8_t)fragment_index,
                                        (uint8_t)fragment_total, &payload[chunk_offset], chunk_len, packet.bytes,
                                        sizeof(packet.bytes));
        if (packet.len == 0U || !EcgSimTxQueuePush(&packet)) {
            g_ecgSimContext.tx_drop_count++;
        }
    }
}

static void EcgSimFlushWaveBatch(void)
{
    uint8_t payload_buf[2U + ECG_SIM_WAVE_BATCH_COUNT * sizeof(uint32_t)];
    uint16_t offset = 0U;
    uint32_t i;

    if (g_ecgSimContext.wave_cache_count == 0U) {
        return;
    }

    /*
     * ECG 逻辑 payload 格式：
     * [sample_count:2B][sample0:4B][sample1:4B]...[sampleN:4B]
     *
     * 这样上位机在重组完分片后，只要读出 sample_count，
     * 就知道后面有多少个 uint32_t ECG payload。
     */
    WriteLe16(&payload_buf[offset], (uint16_t)g_ecgSimContext.wave_cache_count);
    offset += 2U;

    for (i = 0U; i < g_ecgSimContext.wave_cache_count; i++) {
        WriteLe32(&payload_buf[offset], g_ecgSimContext.wave_cache[i]);
        offset += (uint16_t)sizeof(uint32_t);
    }

    EcgSimEnqueueLogicalPayload(MODULE_ECG, DAT_ECG_WAVE, (uint16_t)(g_ecgSimContext.logical_wave_seq & 0xFFFFU),
                                payload_buf, offset);
    g_ecgSimContext.logical_wave_seq++;
    g_ecgSimContext.wave_cache_count = 0U;
}

static void EcgSimEnqueueHeartRate(uint32_t hr_payload)
{
    uint8_t payload_buf[sizeof(uint32_t)];

    WriteLe32(payload_buf, hr_payload);
    EcgSimEnqueueLogicalPayload(MODULE_ECG, DAT_ECG_HR, (uint16_t)(g_ecgSimContext.logical_hr_seq & 0xFFFFU),
                                payload_buf, sizeof(payload_buf));
    g_ecgSimContext.logical_hr_seq++;
}

static void EcgSimProducerTask(void *arg)
{
    uint16_t tick_period;
    uint32_t next_tick;
    uint32_t hr_sample_accumulator = 0U;

    (void)arg;
    osal_msleep(ECG_SIM_STARTUP_DELAY_MS);

    tick_period = MsToTicksCeil(ECG_SIM_PRODUCER_PERIOD_MS);
    if (tick_period == 0U) {
        tick_period = 1U;
    }
    next_tick = osKernelGetTickCount() + tick_period;

    while (1) {
        uint32_t i;

        for (i = 0U; i < ECG_SIM_SAMPLES_PER_PRODUCER_TICK; i++) {
            EcgSimInputItem item = {
                .module_id = MODULE_ECG,
                .payload_type = DAT_ECG_WAVE,
                .payload = EcgSimGenerateWavePayload(&g_ecgSimContext),
            };

            if (!EcgSimInputQueuePush(&item)) {
                g_ecgSimContext.input_drop_count++;
            }

            hr_sample_accumulator++;
            if (hr_sample_accumulator >= ECG_SIM_SAMPLE_RATE_HZ) {
                EcgSimInputItem hr_item = {
                    .module_id = MODULE_ECG,
                    .payload_type = DAT_ECG_HR,
                    .payload = ECG_SIM_DEFAULT_HR_BPM,
                };

                hr_sample_accumulator = 0U;
                g_ecgSimContext.generated_hr_count++;
                if (!EcgSimInputQueuePush(&hr_item)) {
                    g_ecgSimContext.input_drop_count++;
                }
            }
        }

        if ((g_ecgSimContext.generated_ecg_count % (ECG_SIM_SAMPLE_RATE_HZ * 2U)) == 0U) {
            printf("[ecg_sim_producer] ecg=%lu hr=%lu in_drop=%lu wave_cache=%lu\r\n",
                   (unsigned long)g_ecgSimContext.generated_ecg_count,
                   (unsigned long)g_ecgSimContext.generated_hr_count,
                   (unsigned long)g_ecgSimContext.input_drop_count,
                   (unsigned long)g_ecgSimContext.wave_cache_count);
        }

        osDelayUntil(next_tick);
        next_tick += tick_period;
    }
}

static void EcgSimAggregatorTask(void *arg)
{
    (void)arg;

    while (1) {
        EcgSimInputItem item;

        osSemaphoreAcquire(g_ecgSimInputSem, osWaitForever);

        while (EcgSimInputQueuePop(&item)) {
            if (item.module_id != MODULE_ECG) {
                continue;
            }

            if (item.payload_type == DAT_ECG_WAVE) {
                g_ecgSimContext.wave_cache[g_ecgSimContext.wave_cache_count++] = item.payload;

                if (g_ecgSimContext.wave_cache_count >= ECG_SIM_WAVE_BATCH_COUNT) {
                    EcgSimFlushWaveBatch();
                }
            } else if (item.payload_type == DAT_ECG_HR) {
                EcgSimEnqueueHeartRate(item.payload);
            } else {
                /*
                 * 第一版只关心 ECG 波形和心率。
                 * 后续如果你要把 RESP / SPO2 / TEMP 也塞进来，
                 * 就在这里按 module_id + payload_type 扩展分支即可。
                 */
            }
        }
    }
}

static void EcgSimSleTxTask(void *arg)
{
    (void)arg;

    while (1) {
        EcgSimTxPacket packet;

        osSemaphoreAcquire(g_ecgSimTxSem, osWaitForever);

        while (EcgSimTxQueuePeek(&packet)) {
            errcode_t ret;

            if (!sle_uart_client_is_connected()) {
                /*
                 * 没连上上位机时，不要把队头数据弹掉。
                 * 我们保留它，等链路恢复后再继续发。
                 */
                osal_msleep(20U);
                continue;
            }

            if (packet.len > UINT8_MAX) {
                osal_msleep(10U);
                continue;
            }

            ret = (errcode_t)uart_sle_send_data(packet.bytes, (uint8_t)packet.len);
            if (ret != 0) {
                /*
                 * 发送失败也先不丢包，给链路一点恢复时间。
                 */
                osal_msleep(10U);
                continue;
            }

            g_ecgSimContext.sent_packet_count++;
            EcgSimTxQueueConsume();

            if ((g_ecgSimContext.sent_packet_count % ECG_SIM_STATUS_LOG_INTERVAL) == 0U) {
                printf("[ecg_sim_sle] sent=%lu tx_drop=%lu queue_depth=%lu\r\n",
                       (unsigned long)g_ecgSimContext.sent_packet_count,
                       (unsigned long)g_ecgSimContext.tx_drop_count,
                       (unsigned long)(g_ecgSimTxQueue.wr_idx - g_ecgSimTxQueue.rd_idx));
            }
        }
    }
}

static void EcgSleSimulatorEntry(void)
{
    osThreadAttr_t producer_attr = {
        .name = "EcgSimProd",
        .attr_bits = 0U,
        .cb_mem = NULL,
        .cb_size = 0U,
        .stack_mem = NULL,
        .stack_size = ECG_SIM_THREAD_STACK_SIZE,
        .priority = ECG_SIM_THREAD_PRIORITY,
    };
    osThreadAttr_t aggregator_attr = {
        .name = "EcgSimAgg",
        .attr_bits = 0U,
        .cb_mem = NULL,
        .cb_size = 0U,
        .stack_mem = NULL,
        .stack_size = ECG_SIM_THREAD_STACK_SIZE,
        .priority = ECG_SIM_THREAD_PRIORITY,
    };
    osThreadAttr_t sle_attr = {
        .name = "EcgSimSle",
        .attr_bits = 0U,
        .cb_mem = NULL,
        .cb_size = 0U,
        .stack_mem = NULL,
        .stack_size = ECG_SIM_THREAD_STACK_SIZE,
        .priority = ECG_SIM_THREAD_PRIORITY,
    };

    g_ecgSimContext.beat_sample_count = (uint16_t)((ECG_SIM_SAMPLE_RATE_HZ * 60U) / ECG_SIM_DEFAULT_HR_BPM);
    if (g_ecgSimContext.beat_sample_count == 0U) {
        g_ecgSimContext.beat_sample_count = 1U;
    }
    g_ecgSimContext.noise_lcg = 0x13572468U;

    g_ecgSimInputSem = osSemaphoreNew(1U, 0U, NULL);
    g_ecgSimTxSem = osSemaphoreNew(1U, 0U, NULL);
    if (g_ecgSimInputSem == NULL || g_ecgSimTxSem == NULL) {
        printf("[ecg_sim] failed to create semaphores\r\n");
        return;
    }

    if (osThreadNew(EcgSimProducerTask, NULL, &producer_attr) == NULL) {
        printf("[ecg_sim] failed to create producer thread\r\n");
        return;
    }

    if (osThreadNew(EcgSimAggregatorTask, NULL, &aggregator_attr) == NULL) {
        printf("[ecg_sim] failed to create aggregator thread\r\n");
        return;
    }

    if (osThreadNew(EcgSimSleTxTask, NULL, &sle_attr) == NULL) {
        printf("[ecg_sim] failed to create SLE TX thread\r\n");
        return;
    }
}

APP_FEATURE_INIT(EcgSleSimulatorEntry);
