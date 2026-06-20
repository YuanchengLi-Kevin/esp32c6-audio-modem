/*
 * Copyright (c) 2026 Yuancheng Li
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "features/audio_source/audio_source.h"
#include "features/uart_audio/uart_audio.h"

#define AUDIO_UART_NODE DT_CHOSEN(zephyr_audio_uart)

#if !DT_NODE_HAS_STATUS(AUDIO_UART_NODE, okay)
#error "No enabled zephyr,audio-uart chosen node. Check the board devicetree overlay."
#endif

#define SAMPLE_RATE_HZ 42000U
#define FRAME_SAMPLES 128U

int main(void)
{
	const struct device *audio_uart = DEVICE_DT_GET(AUDIO_UART_NODE);
	struct audio_source source;
	struct uart_audio_stream stream;
	uint16_t samples[FRAME_SAMPLES];
	uint64_t stream_start_us;
	uint64_t sample_cursor = 0U;
	int ret;

	if (!device_is_ready(audio_uart))
	{
		printk("Audio UART device is not ready\n");
		return 0;
	}

	sine_source_init(&source, SAMPLE_RATE_HZ);
	ret = uart_audio_stream_init(&stream, audio_uart);
	if (ret != 0)
	{
		printk("Audio UART initialization failed: %d\n", ret);
		return 0;
	}

	printk("ESP32-C6 UART audio streamer: %u Hz, %u samples/frame, 2000000 baud\n",
		   SAMPLE_RATE_HZ, FRAME_SAMPLES);

	stream_start_us = k_ticks_to_us_floor64(k_uptime_ticks());

	while (true)
	{
		source.fill(&source, samples, FRAME_SAMPLES);
		ret = uart_audio_send_frame(&stream, samples, FRAME_SAMPLES);
		if (ret != 0)
		{
			printk("Audio UART transmission failed: %d\n", ret);
		}

		sample_cursor += FRAME_SAMPLES;

		uint64_t target_us = stream_start_us +
							 ((sample_cursor * 1000000ULL) / SAMPLE_RATE_HZ);
		int64_t sleep_us = (int64_t)target_us -
						   (int64_t)k_ticks_to_us_floor64(k_uptime_ticks());

		if (sleep_us > 0)
		{
			k_sleep(K_USEC(sleep_us));
		}
	}

	return 0;
}
