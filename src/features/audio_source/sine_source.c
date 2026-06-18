/*
 * Copyright (c) 2026 Yuancheng Li
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_source.h"

#include <stddef.h>
#include <stdint.h>

#define SINE_TABLE_BITS 6U
#define SINE_TABLE_SIZE (1U << SINE_TABLE_BITS)
#define SINE_TEST_TONE_HZ 656U
#define SINE_AMPLITUDE 1536
#define SAMPLE_MIDPOINT 2048

static const int16_t sine_table[SINE_TABLE_SIZE] = {
	0, 3212, 6393, 9512, 12539, 15446, 18204, 20787,
	23170, 25330, 27245, 28898, 30273, 31357, 32138, 32610,
	32767, 32610, 32138, 31357, 30273, 28898, 27245, 25330,
	23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212,
	0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
	-23170, -25330, -27245, -28898, -30273, -31357, -32138, -32610,
	-32767, -32610, -32138, -31357, -30273, -28898, -27245, -25330,
	-23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212,
};

static void sine_source_fill(struct audio_source *source, uint16_t *samples, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		uint8_t index = source->phase >> (32U - SINE_TABLE_BITS);
		int32_t scaled = (int32_t)sine_table[index] * SINE_AMPLITUDE;
		int32_t sample = SAMPLE_MIDPOINT + (scaled / 32767);

		if (sample < 0) {
			sample = 0;
		} else if (sample > 4095) {
			sample = 4095;
		}

		samples[i] = (uint16_t)sample;
		source->phase += source->phase_step;
	}
}

void sine_source_init(struct audio_source *source, uint32_t sample_rate_hz)
{
	source->fill = sine_source_fill;
	source->sample_rate_hz = sample_rate_hz;
	source->phase = 0U;
	source->phase_step = (uint32_t)(((uint64_t)SINE_TEST_TONE_HZ << 32) / sample_rate_hz);
}
