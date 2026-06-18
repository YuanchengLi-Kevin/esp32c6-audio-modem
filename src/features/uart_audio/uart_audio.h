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

#define UART_AUDIO_MAX_SAMPLES 128U

struct uart_audio_stream {
	const struct device *uart;
	uint8_t sequence;
};

void uart_audio_stream_init(struct uart_audio_stream *stream, const struct device *uart);
void uart_audio_send_frame(struct uart_audio_stream *stream,
			   const uint16_t *samples,
			   size_t sample_count);

#endif /* FEATURES_UART_AUDIO_UART_AUDIO_H_ */
