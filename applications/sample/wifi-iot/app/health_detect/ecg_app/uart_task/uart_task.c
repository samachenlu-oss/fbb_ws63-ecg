#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cmsis_os2.h"
#include "errcode.h"
#include "hal_uart.h"
#include "ohos_init.h"
#define DATA_PACKET_DEFINE_GLOBALS
#include "package.h"
#include "pinctrl.h"
#include "pinctrl_porting.h"
#include "securec.h"
#include "uart.h"

/* ========================================================
 * 物理层与任务配置宏
 * ======================================================== */
#define UART_TEST_TASK_STACK_SIZE 0x1000
#define UART_TEST_TASK_PRIO 25

#define CONFIG_UART_TXD_PIN GPIO_15
#define CONFIG_UART_RXD_PIN GPIO_16

#define UART_BAUDRATE 115200
#define CONFIG_UART_TRANSFER_SIZE 64  // 底层 DMA/FIFO 接收块大小
#define CONFIG_UART_PIN_MODE PIN_MODE_1

/* ========================================================
 * 契约：SPSC 无锁环形缓冲区配置
 * 大小必须是 1024 (2的10次方)，以利用位运算替代取模运算
 * ======================================================== */
#define RB_SIZE 256
#define RB_MASK (RB_SIZE - 1)

/* 数据帧长度 */
#define FRAME_LEN 9

typedef struct
{
    uint8_t buffer[RB_SIZE];
    volatile uint32_t wr_idx;  // 写指针 (仅允许 ISR 修改)
    volatile uint32_t rd_idx;  // 读指针 (仅允许 Task 修改)
} LockFreeRingBuffer_t;

LockFreeRingBuffer_t g_rx_ring = {0};

/* 前后台同步信号量 */
osSemaphoreId_t g_rx_wakeup_sem = NULL;

uint8_t g_app_uart_rx_buff[CONFIG_UART_TRANSFER_SIZE] = {0};
static uart_buffer_config_t g_app_uart_buffer_config = {.rx_buffer = g_app_uart_rx_buff,
                                                        .rx_buffer_size = CONFIG_UART_TRANSFER_SIZE};

// 用于监控由于处理太慢导致的丢包字节数
volatile uint32_t g_overflow_drop_count = 0;

#define CRC8_POLYNOMIAL 0x07  // 经典多项式 x^8 + x^2 + x + 1
#define CRC8_INIT_VALUE 0x00  // 初始寄存器值
static uint8_t Calculate_CRC8(const uint8_t *data, uint16_t length)
{
    uint8_t crc = CRC8_INIT_VALUE;

    // 极度防御：拦截空指针
    if (data == NULL || length == 0) {
        return 0;
    }

    for (uint16_t i = 0; i < length; i++) {
        // 将当前字节与 CRC 寄存器异或
        crc ^= data[i];

        // 对该字节的 8 个比特位逐一处理 (GF(2) 上的多项式长除法)
        for (uint8_t bit = 0; bit < 8; bit++) {
            // 如果最高位是 1，则左移并与多项式异或
            if (crc & 0x80) {
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            }
            // 否则，仅仅左移
            else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/* ========================================================
 * [Producer] 中断上下文：极限榨取算力，溢出防御
 * ======================================================== */
static void UartRxCallback(const void *buffer, uint16_t length, bool error)
{
    if (error || buffer == NULL || length == 0) {
        return;
    }

    const uint8_t *src_data = (const uint8_t *)buffer;

    // 缓存当前指针，防止多次读取 volatile 变量带来的性能损耗
    uint32_t current_wr = g_rx_ring.wr_idx;
    uint32_t current_rd = g_rx_ring.rd_idx;

    // 1. 容量计算与溢出截断 (满足你的第 5 点需求)
    uint32_t used_space = current_wr - current_rd;
    uint32_t available_space = RB_SIZE - used_space;
    uint32_t copy_len = length;

    if (copy_len > available_space) {
        copy_len = available_space;                           // 截断：只填满剩余大小
        g_overflow_drop_count += (length - available_space);  // 记录被无情丢弃的字节数
    }

    if (copy_len == 0) {
        return;  // 缓冲区已彻底爆满，直接丢弃退出
    }

    // 2. 内存折叠拷贝 (计算物理偏移量)
    uint32_t write_pos = current_wr & RB_MASK;
    uint32_t space_to_end = RB_SIZE - write_pos;

    if (copy_len <= space_to_end) {
        memcpy_s(&g_rx_ring.buffer[write_pos], space_to_end, src_data, copy_len);
    } else {
        // 跨越数组尾部，分两次拷贝
        memcpy_s(&g_rx_ring.buffer[write_pos], space_to_end, src_data, space_to_end);
        memcpy_s(&g_rx_ring.buffer[0], RB_SIZE, src_data + space_to_end, copy_len - space_to_end);
    }

    // 3. 极度防御：全量内存屏障
    // 确保 buffer 里的数据彻底落入 RAM 后，再对外发布新的写指针
    __sync_synchronize();

    g_rx_ring.wr_idx = current_wr + copy_len;

    // 4. 唤醒解包任务
    // 因为底层配置了每 32 字节或空闲进一次回调，这里直接释放信号量
    osSemaphoreRelease(g_rx_wakeup_sem);
}

static errcode_t Uart_Init(void)
{
    uapi_pin_set_mode(CONFIG_UART_TXD_PIN, CONFIG_UART_PIN_MODE);
    uapi_pin_set_mode(CONFIG_UART_RXD_PIN, CONFIG_UART_PIN_MODE);

    uart_attr_t attr = {.baud_rate = UART_BAUDRATE,
                        .data_bits = UART_DATA_BIT_8,
                        .stop_bits = UART_STOP_BIT_1,
                        .parity = UART_PARITY_NONE};

    uart_pin_config_t pin_config = {
        .tx_pin = CONFIG_UART_TXD_PIN, .rx_pin = CONFIG_UART_RXD_PIN, .cts_pin = PIN_NONE, .rts_pin = PIN_NONE};

    uapi_uart_deinit(UART_BUS_1);
    errcode_t err = uapi_uart_init(UART_BUS_1, &pin_config, &attr, NULL, &g_app_uart_buffer_config);
    if (err) return err;

    // 【满足第 3 点需求】：配置为接收到 32 字节，或总线空闲（Idle）时触发回调
    uapi_uart_register_rx_callback(UART_BUS_1, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 32, UartRxCallback);

    return ERRCODE_SUCC;
}

static void StateMachine_ParseStream(const uint8_t *stream, uint32_t length)
{
    // 静态缓冲区：用于拼接跨越两次中断的“半截帧”
    static uint8_t frame_buf[FRAME_LEN];
    static uint8_t frame_idx = 0;

    for (uint32_t i = 0; i < length; i++) {
        uint8_t byte = stream[i];

        // 字节级状态机流转
        switch (frame_idx) {
            case 0:  // 锁定 Sync1
                if (byte == 0xAA) {
                    frame_buf[frame_idx++] = byte;
                }
                break;

            case 1:  // 锁定 Sync2
                if (byte == 0x55) {
                    frame_buf[frame_idx++] = byte;
                } else if (byte == 0xAA) {
                    // 抵御 0xAA 0xAA 0x55 这种连绵干扰
                    frame_idx = 1;
                } else {
                    frame_idx = 0;  // 同步失败，丢弃并重置
                }
                break;

            default:  // 无脑灌入后续字节 (索引 2 到 8)
                frame_buf[frame_idx++] = byte;
                // 攒齐 9 个字节，触发校验与解包
                if (frame_idx == FRAME_LEN) {
                    /* 校验码的计算 */
                    uint8_t check_sum = 0;
                    for (int i = 0; i < FRAME_LEN - 1; i++) {
                        check_sum += frame_buf[i];
                    }
                    /* 检测校验码 */
                    if (check_sum == frame_buf[8]) {
                        // 1. 提取模块 ID
                        uint8_t module_id = frame_buf[2];
                        printf("mid:0x%02X ", frame_buf[2]);
                        // 只处理心电模块的数据
                        if (module_id == MODULE_ECG) {
                            // 2. 提取 Type (现在是 1 个字节，直接读取)
                            uint8_t type = (uint16_t)frame_buf[3];
                            printf("type:0x%02X ", frame_buf[3]);
                            printf("\r\n");
                            // 3. 提取 Payload (架构师的性能魔法)
                            // 因为 Payload 从索引 4 开始，是绝对的 32 位对齐地址。
                            // 在 WS63 (默认小端) 和 STM32 (小端) 互通时，
                            // 我们可以直接通过指针强转零成本获取数据，省去 4 次移位和 3 次或运算！
                            uint32_t payload = *((uint32_t *)(&frame_buf[4]));
                            // 这一块仅仅用于测试
                            printf("payload:%u ", payload);
                            printf("\r\n");

                            // 4. 送入第二级结构体环形队列 (供 MQTT 任务消费)
                            uint32_t current_wr = g_parsed_queue.wr_idx;
                            uint32_t current_rd = g_parsed_queue.rd_idx;

                            // 检查队列背压
                            if (current_wr - current_rd < PARSED_Q_SIZE) {
                                uint32_t write_pos = current_wr & PARSED_Q_MASK;

                                g_parsed_queue.buffer[write_pos].type = type;
                                g_parsed_queue.buffer[write_pos].payload = payload;

                                // 必须加屏障，确保 payload 写完后再推指针
                                __sync_synchronize();

                                g_parsed_queue.wr_idx = current_wr + 1;

                                // (可选) osSemaphoreRelease(g_mqtt_wakeup_sem);
                            } else {
                                // 队列已满：说明 MQTT 发送线程卡死了
                                // 工业级做法：静默丢弃，保证解析线程不被拖死
                            }
                        }
                    } else {
                        // CRC 失败：字节流发生了物理损坏，直接丢弃该帧
                    }
                    // 无论成功还是失败，状态机清零，准备捕捉下一个 0xAA
                    frame_idx = 0;
                }
                break;
        }
    }
}

/* ========================================================
 * [Consumer] 任务上下文：解包状态机的温床
 * ======================================================== */
void UartUnpackTask(const char *arg)
{
    (void)arg;

    // 创建二值信号量
    g_rx_wakeup_sem = osSemaphoreNew(1, 0, NULL);
    if (g_rx_wakeup_sem == NULL) {
        printf("[Fatal] Failed to create semaphore!\r\n");
        return;
    }

    if (Uart_Init() != ERRCODE_SUCC) {
        printf("[Fatal] UART Init Failed!\r\n");
        return;
    }

    printf("UART Init Success. Unpack Task is waiting for stream...\r\n");

    // 从 RingBuffer 拉取到任务私有栈上的缓冲区，供状态机处理
    uint8_t process_buf[64];

    while (1) {
        // 让出 CPU，死等底层 ISR 的数据唤醒
        osSemaphoreAcquire(g_rx_wakeup_sem, osWaitForever);

        // 醒来后，必须用 while 循环把 RingBuffer 彻底“榨干”，防止二值信号量丢失导致数据残留
        while (1) {
            uint32_t current_rd = g_rx_ring.rd_idx;
            uint32_t current_wr = g_rx_ring.wr_idx;

            uint32_t available = current_wr - current_rd;
            if (available == 0) {
                break;  // 榨干了，退出内部循环，继续挂起等待
            }

            // 单次提取数量限制，防止占用过久
            uint32_t fetch_len = (available > sizeof(process_buf)) ? sizeof(process_buf) : available;

            // 计算物理映射读取
            uint32_t read_pos = current_rd & RB_MASK;
            uint32_t space_to_end = RB_SIZE - read_pos;

            if (fetch_len <= space_to_end) {
                memcpy_s(process_buf, sizeof(process_buf), &g_rx_ring.buffer[read_pos], fetch_len);
            } else {
                memcpy_s(process_buf, sizeof(process_buf), &g_rx_ring.buffer[read_pos], space_to_end);
                memcpy_s(process_buf + space_to_end, sizeof(process_buf) - space_to_end, &g_rx_ring.buffer[0],
                         fetch_len - space_to_end);
            }

            // 极度防御：确保我们读取完物理数据后，再向外发布新的 rd_idx
            __sync_synchronize();

            // 推进读指针，将这部分物理内存归还给 ISR
            g_rx_ring.rd_idx = current_rd + fetch_len;

            // ====================================================
            // 🚨 这里是你的主场：数据已准备好，安全地喂给解包状态机
            // ====================================================
            StateMachine_ParseStream(process_buf, fetch_len);

            // [测试打印] 证明数据收到了。正式代码请注释掉，printf 极度耗时！
            // printf("Unpack Task Pulled %u bytes.\r\n", fetch_len);
        }
    }
}

static void UartTestEntry(void)
{
    osThreadAttr_t attr = {
        .name = "UnpackTask",
        .stack_size = UART_TEST_TASK_STACK_SIZE,
        .priority = UART_TEST_TASK_PRIO,
    };

    if (osThreadNew((osThreadFunc_t)UartUnpackTask, NULL, &attr) == NULL) {
        printf("[UART_TEST] Falied to create UnpackTask!\n");
    }
}

SYS_RUN(UartTestEntry);
