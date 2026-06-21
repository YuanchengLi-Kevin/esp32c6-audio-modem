/*
 * Copyright (c) 2026 Yuancheng Li
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FEATURES_JITTER_BUFFER_JITTER_BUFFER_H_
#define FEATURES_JITTER_BUFFER_JITTER_BUFFER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#define JITTER_BUFFER_MAX_SAMPLES 128U
#define JITTER_BUFFER_SILENCE 2048U

struct jitter_buffer_packet {
	uint16_t samples[JITTER_BUFFER_MAX_SAMPLES];
	uint16_t sequence;
	uint16_t sample_count;
	uint16_t read_offset;
	bool valid;
};

struct jitter_buffer {
	struct k_mutex lock;
	struct jitter_buffer_packet packets[CONFIG_AUDIO_MODEM_JITTER_BUFFER_PACKETS];
	uint16_t next_sequence;
	uint16_t packet_count;
	bool sequence_valid;
	bool started;
};

void jitter_buffer_init(struct jitter_buffer *buffer);
int jitter_buffer_push(struct jitter_buffer *buffer, uint16_t sequence,
		       const uint16_t *samples, size_t sample_count);
bool jitter_buffer_pop(struct jitter_buffer *buffer, uint16_t *samples, size_t sample_count);

#endif /* FEATURES_JITTER_BUFFER_JITTER_BUFFER_H_ */
