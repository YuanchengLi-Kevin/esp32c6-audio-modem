/*
 * Copyright (c) 2026 Yuancheng Li
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart_audio.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define FRAME_SYNC0 0xA5U
#define FRAME_SYNC1 0x5AU

static void uart_audio_callback(const struct device *uart,
				struct uart_event *event,
				void *user_data)
{
	struct uart_audio_stream *stream = user_data;

	ARG_UNUSED(uart);

	if ((event->type == UART_TX_DONE) || (event->type == UART_TX_ABORTED)) {
		k_sem_give(&stream->tx_available);
	}
}

int uart_audio_stream_init(struct uart_audio_stream *stream, const struct device *uart)
{
	if ((stream == NULL) || (uart == NULL)) {
		return -EINVAL;
	}

	stream->uart = uart;
	stream->sequence = 0U;
	k_sem_init(&stream->tx_available, 1U, 1U);

	return uart_callback_set(uart, uart_audio_callback, stream);
}

int uart_audio_send_frame(struct uart_audio_stream *stream,
			  const uint16_t *samples,
			  size_t sample_count)
{
	int ret;
	size_t frame_size;

	if ((stream == NULL) || (stream->uart == NULL) || (samples == NULL) ||
	    (sample_count == 0U) || (sample_count > UART_AUDIO_MAX_SAMPLES)) {
		return -EINVAL;
	}

	ret = k_sem_take(&stream->tx_available, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	stream->frame[0] = FRAME_SYNC0;
	stream->frame[1] = FRAME_SYNC1;
	stream->frame[2] = stream->sequence;
	stream->frame[3] = (uint8_t)sample_count;

	for (size_t i = 0; i < sample_count; i++) {
		uint16_t sample = MIN(samples[i], 4095U);

		stream->frame[UART_AUDIO_FRAME_HEADER_SIZE + (i * 2U)] =
			(uint8_t)(sample & 0xffU);
		stream->frame[UART_AUDIO_FRAME_HEADER_SIZE + (i * 2U) + 1U] =
			(uint8_t)(sample >> 8);
	}

	frame_size = UART_AUDIO_FRAME_HEADER_SIZE + (sample_count * sizeof(uint16_t));
	ret = uart_tx(stream->uart, stream->frame, frame_size, SYS_FOREVER_US);
	if (ret != 0) {
		k_sem_give(&stream->tx_available);
		return ret;
	}

	stream->sequence++;
	return 0;
}
