/*
 * Copyright (c) 2026 Yuancheng Li
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart_audio.h"

#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/__assert.h>

#define FRAME_SYNC0 0xA5U
#define FRAME_SYNC1 0x5AU

static void uart_audio_put_u8(const struct device *uart, uint8_t value)
{
	uart_poll_out(uart, value);
}

static void uart_audio_put_le16(const struct device *uart, uint16_t value)
{
	uart_audio_put_u8(uart, (uint8_t)(value & 0xffU));
	uart_audio_put_u8(uart, (uint8_t)(value >> 8));
}

void uart_audio_stream_init(struct uart_audio_stream *stream, const struct device *uart)
{
	stream->uart = uart;
	stream->sequence = 0U;
}

void uart_audio_send_frame(struct uart_audio_stream *stream,
			   const uint16_t *samples,
			   size_t sample_count)
{
	__ASSERT_NO_MSG(sample_count > 0U);
	__ASSERT_NO_MSG(sample_count <= UART_AUDIO_MAX_SAMPLES);

	uart_audio_put_u8(stream->uart, FRAME_SYNC0);
	uart_audio_put_u8(stream->uart, FRAME_SYNC1);
	uart_audio_put_u8(stream->uart, stream->sequence);
	uart_audio_put_u8(stream->uart, (uint8_t)sample_count);

	for (size_t i = 0; i < sample_count; i++) {
		uart_audio_put_le16(stream->uart, samples[i]);
	}

	stream->sequence++;
}
