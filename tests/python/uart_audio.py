# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

"""Decoder for the ESP32-C6 UART audio wire protocol."""

from dataclasses import dataclass
from typing import BinaryIO

FRAME_SYNC = b"\xA5\x5A"
MAX_SAMPLES = 128


@dataclass(frozen=True)
class AudioFrame:
    sequence: int
    samples: tuple[int, ...]


class AudioFrameReader:
    """Read framed PCM data from a binary stream, including ``serial.Serial``."""

    def __init__(self, stream: BinaryIO):
        self._stream = stream

    def read_frame(self) -> AudioFrame:
        self._find_sync()
        header = self._read_exact(2)
        sequence = header[0]
        sample_count = header[1]

        if not 1 <= sample_count <= MAX_SAMPLES:
            raise ValueError(f"invalid sample count: {sample_count}")

        payload = self._read_exact(sample_count * 2)
        samples = tuple(
            int.from_bytes(payload[offset : offset + 2], "little")
            for offset in range(0, len(payload), 2)
        )
        return AudioFrame(sequence=sequence, samples=samples)

    def _find_sync(self) -> None:
        matched_first_byte = False

        while True:
            value = self._read_exact(1)[0]
            if not matched_first_byte:
                matched_first_byte = value == FRAME_SYNC[0]
            elif value == FRAME_SYNC[1]:
                return
            else:
                matched_first_byte = value == FRAME_SYNC[0]

    def _read_exact(self, size: int) -> bytes:
        data = bytearray()

        while len(data) < size:
            chunk = self._stream.read(size - len(data))
            if not chunk:
                raise TimeoutError(
                    f"stream ended after {len(data)} of {size} required bytes"
                )
            data.extend(chunk)

        return bytes(data)
