/*
 * Copyright (c) 2026 Yuancheng Li
 * SPDX-License-Identifier: Apache-2.0
 */

#include "jitter_buffer.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>

BUILD_ASSERT(CONFIG_AUDIO_MODEM_JITTER_BUFFER_START_PACKETS <=
	     CONFIG_AUDIO_MODEM_JITTER_BUFFER_PACKETS,
	     "Jitter buffer startup depth cannot exceed its capacity");

static bool sequence_is_before(uint16_t sequence, uint16_t reference)
{
	return (uint16_t)(sequence - reference) > 0x7fffU;
}

void jitter_buffer_init(struct jitter_buffer *buffer)
{
	memset(buffer, 0, sizeof(*buffer));
	k_mutex_init(&buffer->lock);
}

int jitter_buffer_push(struct jitter_buffer *buffer, uint16_t sequence,
		       const uint16_t *samples, size_t sample_count)
{
	struct jitter_buffer_packet *packet;
	uint16_t distance;
	int ret = 0;

	if ((buffer == NULL) || (samples == NULL) || (sample_count == 0U) ||
	    (sample_count > JITTER_BUFFER_MAX_SAMPLES)) {
		return -EINVAL;
	}

	k_mutex_lock(&buffer->lock, K_FOREVER);

	if (!buffer->sequence_valid) {
		buffer->next_sequence = sequence;
		buffer->sequence_valid = true;
	}

	if (sequence_is_before(sequence, buffer->next_sequence)) {
		if ((buffer->packet_count == 0U) && !buffer->started) {
			buffer->next_sequence = sequence;
		} else {
			ret = -ESTALE;
			goto unlock;
		}
	}

	distance = sequence - buffer->next_sequence;
	if (distance >= CONFIG_AUDIO_MODEM_JITTER_BUFFER_PACKETS) {
		if (buffer->packet_count != 0U) {
			ret = -ENOBUFS;
			goto unlock;
		}

		buffer->next_sequence = sequence;
		buffer->started = false;
	}

	packet = &buffer->packets[sequence % CONFIG_AUDIO_MODEM_JITTER_BUFFER_PACKETS];
	if (packet->valid) {
		ret = (packet->sequence == sequence) ? -EALREADY : -ENOBUFS;
		goto unlock;
	}

	memcpy(packet->samples, samples, sample_count * sizeof(samples[0]));
	packet->sequence = sequence;
	packet->sample_count = sample_count;
	packet->read_offset = 0U;
	packet->valid = true;
	buffer->packet_count++;

unlock:
	k_mutex_unlock(&buffer->lock);
	return ret;
}

bool jitter_buffer_pop(struct jitter_buffer *buffer, uint16_t *samples, size_t sample_count)
{
	struct jitter_buffer_packet *packet;
	size_t copied = 0U;
	bool complete = true;

	if ((buffer == NULL) || (samples == NULL) || (sample_count == 0U)) {
		return false;
	}

	for (size_t i = 0U; i < sample_count; i++) {
		samples[i] = JITTER_BUFFER_SILENCE;
	}

	k_mutex_lock(&buffer->lock, K_FOREVER);

	if (!buffer->started) {
		if (buffer->packet_count < CONFIG_AUDIO_MODEM_JITTER_BUFFER_START_PACKETS) {
			complete = false;
			goto unlock;
		}
		buffer->started = true;
	}

	while (copied < sample_count) {
		packet = &buffer->packets[buffer->next_sequence %
					 CONFIG_AUDIO_MODEM_JITTER_BUFFER_PACKETS];
		if (!packet->valid || (packet->sequence != buffer->next_sequence)) {
			buffer->next_sequence++;
			if (buffer->packet_count == 0U) {
				buffer->started = false;
			}
			complete = false;
			break;
		}

		size_t available = packet->sample_count - packet->read_offset;
		size_t amount = MIN(available, sample_count - copied);

		memcpy(&samples[copied], &packet->samples[packet->read_offset],
		       amount * sizeof(samples[0]));
		packet->read_offset += amount;
		copied += amount;

		if (packet->read_offset == packet->sample_count) {
			packet->valid = false;
			buffer->packet_count--;
			buffer->next_sequence++;
		}
	}

unlock:
	k_mutex_unlock(&buffer->lock);
	return complete;
}
