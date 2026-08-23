/*
 * Copyright (c) 2026 Yuancheng Li
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart_audio.h"

#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>

#define FRAME_SYNC0 0xA5U
#define FRAME_SYNC1 0x5AU
#define FRAME_CRC_INITIAL 0xFFFFU
#define FRAME_CRC_POLYNOMIAL 0x1021U

static uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
	uint16_t crc = FRAME_CRC_INITIAL;

	for (size_t i = 0; i < length; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (uint8_t bit = 0; bit < 8; bit++) {
			crc = (crc & 0x8000U) != 0U ?
				(uint16_t)((crc << 1) ^ FRAME_CRC_POLYNOMIAL) :
				(uint16_t)(crc << 1);
		}
	}

	return crc;
}

int uart_audio_stream_init(struct uart_audio_stream *stream, const struct device *uart)
{
	if ((stream == NULL) || (uart == NULL)) {
		return -EINVAL;
	}

	stream->uart = uart;
	stream->sequence = 0U;
	atomic_set(&stream->completed_frames, 0);

	return 0;
}

int uart_audio_send_frame(struct uart_audio_stream *stream,
			  const uint16_t *samples,
			  size_t sample_count)
{
	size_t frame_size;
	uint16_t crc;

	if ((stream == NULL) || (stream->uart == NULL) || (samples == NULL) ||
	    (sample_count == 0U) || (sample_count > UART_AUDIO_MAX_SAMPLES)) {
		return -EINVAL;
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
	crc = crc16_ccitt(&stream->frame[2], frame_size - 2U);
	stream->frame[frame_size] = (uint8_t)(crc & 0xffU);
	stream->frame[frame_size + 1U] = (uint8_t)(crc >> 8);
	frame_size += UART_AUDIO_FRAME_CRC_SIZE;

	for (size_t i = 0; i < frame_size; i++) {
		uart_poll_out(stream->uart, stream->frame[i]);
	}

	stream->sequence++;
	atomic_inc(&stream->completed_frames);
	return 0;
}

uint32_t uart_audio_completed_frames(const struct uart_audio_stream *stream)
{
	return (uint32_t)atomic_get(&stream->completed_frames);
}
