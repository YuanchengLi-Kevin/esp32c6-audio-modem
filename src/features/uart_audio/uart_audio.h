/*
 * Copyright (c) 2026 Yuancheng Li
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FEATURES_UART_AUDIO_UART_AUDIO_H_
#define FEATURES_UART_AUDIO_UART_AUDIO_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#define UART_AUDIO_MAX_SAMPLES 128U
#define UART_AUDIO_FRAME_HEADER_SIZE 4U
#define UART_AUDIO_MAX_FRAME_SIZE \
	(UART_AUDIO_FRAME_HEADER_SIZE + (UART_AUDIO_MAX_SAMPLES * sizeof(uint16_t)))

struct uart_audio_stream {
	const struct device *uart;
	struct k_sem tx_available;
	uint8_t frame[UART_AUDIO_MAX_FRAME_SIZE];
	uint8_t sequence;
};

int uart_audio_stream_init(struct uart_audio_stream *stream, const struct device *uart);
int uart_audio_send_frame(struct uart_audio_stream *stream,
			  const uint16_t *samples,
			  size_t sample_count);

#endif /* FEATURES_UART_AUDIO_UART_AUDIO_H_ */
