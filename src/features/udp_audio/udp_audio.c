/*
 * Copyright (c) 2026 Yuancheng Li
 * SPDX-License-Identifier: Apache-2.0
 */

#include "udp_audio.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>

#include "features/jitter_buffer/jitter_buffer.h"

LOG_MODULE_REGISTER(udp_audio, LOG_LEVEL_INF);

#define UDP_AUDIO_HEADER_SIZE 10U
#define UDP_AUDIO_PACKET_SIZE (UDP_AUDIO_HEADER_SIZE + (JITTER_BUFFER_MAX_SAMPLES * 2U))
#define UDP_AUDIO_VERSION 1U
#define UDP_AUDIO_THREAD_STACK_SIZE 3072U
#define UDP_AUDIO_THREAD_PRIORITY 5

static const uint8_t packet_magic[4] = {'A', 'U', 'D', '0'};
static struct jitter_buffer *audio_buffer;
static struct k_thread receiver_thread;
K_THREAD_STACK_DEFINE(receiver_stack, UDP_AUDIO_THREAD_STACK_SIZE);

static int parse_packet(const uint8_t *packet, size_t length)
{
	uint16_t samples[JITTER_BUFFER_MAX_SAMPLES];
	uint16_t sequence;
	uint16_t sample_count;

	if ((length < UDP_AUDIO_HEADER_SIZE) ||
	    (memcmp(packet, packet_magic, sizeof(packet_magic)) != 0) ||
	    (packet[4] != UDP_AUDIO_VERSION) || (packet[5] != 0U)) {
		return -EBADMSG;
	}

	sequence = sys_get_le16(&packet[6]);
	sample_count = sys_get_le16(&packet[8]);
	if ((sample_count != JITTER_BUFFER_MAX_SAMPLES) ||
	    (length != UDP_AUDIO_HEADER_SIZE + (sample_count * sizeof(int16_t)))) {
		return -EMSGSIZE;
	}

	for (size_t i = 0U; i < sample_count; i++) {
		int16_t pcm = (int16_t)sys_get_le16(&packet[UDP_AUDIO_HEADER_SIZE + (i * 2U)]);
		samples[i] = (uint16_t)(((int32_t)pcm + 32768) >> 4);
	}

	return jitter_buffer_push(audio_buffer, sequence, samples, sample_count);
}

static void receiver_entry(void *unused1, void *unused2, void *unused3)
{
	uint8_t packet[UDP_AUDIO_PACKET_SIZE];
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(CONFIG_AUDIO_MODEM_UDP_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	while (true) {
		int socket = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socket < 0) {
			LOG_ERR("UDP socket creation failed: %d", errno);
			k_sleep(K_SECONDS(1));
			continue;
		}

		if (zsock_bind(socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
			LOG_ERR("UDP bind on port %d failed: %d",
				CONFIG_AUDIO_MODEM_UDP_PORT, errno);
			zsock_close(socket);
			k_sleep(K_SECONDS(1));
			continue;
		}

		LOG_INF("Listening for UDP audio on port %d", CONFIG_AUDIO_MODEM_UDP_PORT);
		while (true) {
			ssize_t received = zsock_recv(socket, packet, sizeof(packet), 0);
			if (received < 0) {
				LOG_ERR("UDP receive failed: %d", errno);
				break;
			}

			int ret = parse_packet(packet, received);
			if ((ret != 0) && (ret != -EALREADY) && (ret != -ESTALE) &&
			    (ret != -ENOBUFS)) {
				LOG_WRN("Dropped UDP audio packet: %d", ret);
			}
		}

		zsock_close(socket);
		k_sleep(K_MSEC(250));
	}
}

int udp_audio_start(struct jitter_buffer *buffer)
{
	if ((buffer == NULL) || (audio_buffer != NULL)) {
		return -EINVAL;
	}

	audio_buffer = buffer;
	k_thread_create(&receiver_thread, receiver_stack,
			K_THREAD_STACK_SIZEOF(receiver_stack), receiver_entry,
			NULL, NULL, NULL, UDP_AUDIO_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&receiver_thread, "udp_audio");

	return 0;
}
