/*
 * Copyright (c) 2026 Yuancheng Li
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FEATURES_AUDIO_SOURCE_AUDIO_SOURCE_H_
#define FEATURES_AUDIO_SOURCE_AUDIO_SOURCE_H_

#include <stddef.h>
#include <stdint.h>

struct audio_source {
	void (*fill)(struct audio_source *source, uint16_t *samples, size_t count);
	uint32_t sample_rate_hz;
	uint32_t phase;
	uint32_t phase_step;
};

void sine_source_init(struct audio_source *source, uint32_t sample_rate_hz);

#endif /* FEATURES_AUDIO_SOURCE_AUDIO_SOURCE_H_ */
