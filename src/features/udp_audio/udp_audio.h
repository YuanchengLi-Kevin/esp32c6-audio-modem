/*
 * Copyright (c) 2026 Yuancheng Li
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FEATURES_UDP_AUDIO_UDP_AUDIO_H_
#define FEATURES_UDP_AUDIO_UDP_AUDIO_H_

struct jitter_buffer;

int udp_audio_start(struct jitter_buffer *buffer);

#endif /* FEATURES_UDP_AUDIO_UDP_AUDIO_H_ */
