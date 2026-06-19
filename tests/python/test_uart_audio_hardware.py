# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

import os

import pytest

from uart_audio import AudioFrameReader

serial = pytest.importorskip("serial")

SAMPLE_RATE_HZ = 42_000
SAMPLE_MIDPOINT = 2048


@pytest.fixture(scope="module")
def frame_reader():
    port = os.getenv("AUDIO_UART_PORT")
    if not port:
        pytest.skip("set AUDIO_UART_PORT to run tests against hardware")

    with serial.Serial(port, 2_000_000, timeout=2) as stream:
        stream.reset_input_buffer()
        yield AudioFrameReader(stream)


@pytest.mark.hardware
def test_hardware_frame_format(frame_reader: AudioFrameReader) -> None:
    frame = frame_reader.read_frame()

    assert len(frame.samples) == 128
    assert all(0 <= sample <= 4095 for sample in frame.samples)


@pytest.mark.hardware
def test_hardware_sequence(frame_reader: AudioFrameReader) -> None:
    sequences = [frame_reader.read_frame().sequence for _ in range(12)]

    assert all(
        current == (previous + 1) & 0xFF
        for previous, current in zip(sequences, sequences[1:])
    )


@pytest.mark.hardware
def test_hardware_sine_wave(frame_reader: AudioFrameReader) -> None:
    samples: list[int] = []
    while len(samples) < SAMPLE_RATE_HZ:
        samples.extend(frame_reader.read_frame().samples)
    samples = samples[:SAMPLE_RATE_HZ]

    assert min(samples) == pytest.approx(512, abs=20)
    assert max(samples) == pytest.approx(3584, abs=20)
    assert sum(samples) / len(samples) == pytest.approx(SAMPLE_MIDPOINT, abs=10)

    upward_crossings = sum(
        previous < SAMPLE_MIDPOINT <= current
        for previous, current in zip(samples, samples[1:])
    )
    assert upward_crossings == pytest.approx(656, abs=2)
