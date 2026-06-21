/*
 * Copyright (c) 2026 Yuancheng Li
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "features/uart_audio/uart_audio.h"

#if defined(CONFIG_AUDIO_MODEM_SOURCE_SINE)
#include "features/audio_source/audio_source.h"
#else
#include "features/jitter_buffer/jitter_buffer.h"
#include "features/udp_audio/udp_audio.h"
#include "features/wifi_station/wifi_station.h"
#endif

#define AUDIO_UART_NODE DT_CHOSEN(zephyr_audio_uart)

#if !DT_NODE_HAS_STATUS(AUDIO_UART_NODE, okay)
#error "No enabled zephyr,audio-uart chosen node. Check the board devicetree overlay."
#endif

#define SAMPLE_RATE_HZ 42000U
#define FRAME_SAMPLES 128U

#if defined(CONFIG_AUDIO_MODEM_SOURCE_UDP)
static struct jitter_buffer jitter_buffer;
#endif

int main(void)
{
	const struct device *audio_uart = DEVICE_DT_GET(AUDIO_UART_NODE);
	struct uart_audio_stream stream;
#if defined(CONFIG_AUDIO_MODEM_SOURCE_SINE)
	struct audio_source source;
#endif
	uint16_t samples[FRAME_SAMPLES];
	uint64_t stream_start_us;
	uint64_t sample_cursor = 0U;
	int ret;

	if (!device_is_ready(audio_uart))
	{
		printk("Audio UART device is not ready\n");
		return 0;
	}

	ret = uart_audio_stream_init(&stream, audio_uart);
	if (ret != 0)
	{
		printk("Audio UART initialization failed: %d\n", ret);
		return 0;
	}

#if defined(CONFIG_AUDIO_MODEM_SOURCE_SINE)
	sine_source_init(&source, SAMPLE_RATE_HZ);
#else
	jitter_buffer_init(&jitter_buffer);
	ret = wifi_station_init();
	if (ret != 0) {
		printk("Wi-Fi station initialization failed: %d\n", ret);
		return 0;
	}

	ret = udp_audio_start(&jitter_buffer);
	if (ret != 0) {
		printk("UDP audio initialization failed: %d\n", ret);
		return 0;
	}

	ret = wifi_station_connect();
	if (ret != 0) {
		printk("Wi-Fi connection request failed: %d\n", ret);
	}
#endif

	printk("ESP32-C6 UART audio streamer: %u Hz, %u samples/frame, 1000000 baud\n",
		   SAMPLE_RATE_HZ, FRAME_SAMPLES);

	stream_start_us = k_ticks_to_us_floor64(k_uptime_ticks());

	while (true)
	{
#if defined(CONFIG_AUDIO_MODEM_SOURCE_SINE)
		source.fill(&source, samples, FRAME_SAMPLES);
#else
		jitter_buffer_pop(&jitter_buffer, samples, FRAME_SAMPLES);
#endif
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
