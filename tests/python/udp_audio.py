# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

"""Encoder for the ESP32-C6 UDP audio packet format."""

import struct
from collections.abc import Sequence

PACKET_MAGIC = b"AUD0"
PACKET_VERSION = 1
MAX_SAMPLES = 128


def encode_udp_audio(sequence: int, samples: Sequence[int]) -> bytes:
    """Encode signed 16-bit PCM samples into one UDP audio datagram."""
    if not 0 <= sequence <= 0xFFFF:
        raise ValueError("sequence must fit in uint16")
    if len(samples) != MAX_SAMPLES:
        raise ValueError(f"sample count must be exactly {MAX_SAMPLES}")
    if any(not -32768 <= sample <= 32767 for sample in samples):
        raise ValueError("samples must fit in int16")

    header = PACKET_MAGIC + struct.pack("<BBHH", PACKET_VERSION, 0, sequence, len(samples))
    return header + struct.pack(f"<{len(samples)}h", *samples)
