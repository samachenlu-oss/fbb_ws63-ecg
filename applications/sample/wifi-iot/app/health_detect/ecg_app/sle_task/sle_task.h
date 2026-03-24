#ifndef SLE_TASK_H
#define SLE_TASK_H

#include <stdint.h>

#include "errcode.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

errcode_t sle_uart_server_init(void);

errcode_t sle_uart_server_send_report_by_uuid(const uint8_t *data, uint8_t len);

errcode_t sle_uart_server_send_report_by_handle(const uint8_t *data, uint8_t len);

uint16_t sle_uart_client_is_connected(void);

typedef void (*sle_uart_server_msg_queue)(uint8_t *buffer_addr, uint16_t buffer_size);

void sle_uart_server_register_msg(sle_uart_server_msg_queue sle_uart_server_msg);

errcode_t sle_enable_server_cbk(void);

int uart_sle_send_data(uint8_t *data, uint8_t length);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
