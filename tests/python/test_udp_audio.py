# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

import struct

import pytest

from udp_audio import encode_udp_audio


def test_encodes_udp_header_and_signed_pcm() -> None:
    samples = [-32768, 0, 32767] + [0] * 125
    packet = encode_udp_audio(0x1234, samples)

    assert packet[:4] == b"AUD0"
    assert packet[4:6] == b"\x01\x00"
    assert struct.unpack("<HH", packet[6:10]) == (0x1234, 128)
    assert struct.unpack("<hhh", packet[10:16]) == (-32768, 0, 32767)


@pytest.mark.parametrize("samples", [[], [0], [0] * 127, [0] * 129])
def test_rejects_invalid_sample_count(samples: list[int]) -> None:
    with pytest.raises(ValueError, match="sample count"):
        encode_udp_audio(0, samples)


def test_rejects_out_of_range_values() -> None:
    with pytest.raises(ValueError, match="sequence"):
        encode_udp_audio(65536, [0] * 128)

    with pytest.raises(ValueError, match="samples"):
        encode_udp_audio(0, [32768] + [0] * 127)
