# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

from io import BytesIO

import pytest

from uart_audio import AudioFrameReader


def encode_frame(sequence: int, samples: list[int]) -> bytes:
    payload = b"".join(sample.to_bytes(2, "little") for sample in samples)
    return b"\xA5\x5A" + bytes((sequence, len(samples))) + payload


def test_decodes_frame_and_little_endian_samples() -> None:
    reader = AudioFrameReader(BytesIO(encode_frame(7, [0x0123, 0x0ABC])))

    frame = reader.read_frame()

    assert frame.sequence == 7
    assert frame.samples == (0x0123, 0x0ABC)


def test_ignores_noise_before_sync() -> None:
    data = b"\x00\x5A\xA5\xA5" + encode_frame(3, [2048])
    reader = AudioFrameReader(BytesIO(data))

    frame = reader.read_frame()

    assert frame.sequence == 3
    assert frame.samples == (2048,)


def test_reads_consecutive_frames_and_sequence_rollover() -> None:
    data = encode_frame(255, [1]) + encode_frame(0, [2])
    reader = AudioFrameReader(BytesIO(data))

    first = reader.read_frame()
    second = reader.read_frame()

    assert first.sequence == 255
    assert second.sequence == 0
    assert second.samples == (2,)


@pytest.mark.parametrize("sample_count", [0, 129])
def test_rejects_invalid_sample_count(sample_count: int) -> None:
    reader = AudioFrameReader(BytesIO(b"\xA5\x5A\x00" + bytes((sample_count,))))

    with pytest.raises(ValueError, match="invalid sample count"):
        reader.read_frame()


def test_reports_truncated_payload() -> None:
    reader = AudioFrameReader(BytesIO(b"\xA5\x5A\x00\x02\x00\x08"))

    with pytest.raises(TimeoutError, match="2 of 4 required bytes"):
        reader.read_frame()
