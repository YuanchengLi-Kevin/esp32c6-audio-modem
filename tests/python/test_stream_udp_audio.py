# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

import io
import struct
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest


TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import stream_udp_audio as sender  # noqa: E402


class FakeSocket:
    def __init__(self) -> None:
        self.packets: list[tuple[bytes, tuple[str, int]]] = []

    def __enter__(self) -> "FakeSocket":
        return self

    def __exit__(self, *args: object) -> None:
        return None

    def sendto(self, packet: bytes, address: tuple[str, int]) -> None:
        self.packets.append((packet, address))


def test_parse_args_selects_file_source() -> None:
    args = sender.parse_args(
        ["--host", "192.0.2.10", "--port", "5000", "--file", "song.mp3"]
    )

    assert args.host == "192.0.2.10"
    assert args.port == 5000
    assert args.file == Path("song.mp3")
    assert not args.system_audio


def test_parse_args_selects_system_audio_source() -> None:
    args = sender.parse_args(["--host", "192.0.2.10", "--system-audio"])

    assert args.file is None
    assert args.system_audio
    assert args.ffmpeg == "ffmpeg"


@pytest.mark.parametrize(
    "source_args",
    [[], ["--file", "song.mp3", "--system-audio"]],
)
def test_parse_args_requires_exactly_one_source(source_args: list[str]) -> None:
    with pytest.raises(SystemExit):
        sender.parse_args(["--host", "192.0.2.10", *source_args])


def test_send_pcm_stream_paces_packets_and_wraps_sequence() -> None:
    pcm = struct.pack("<256h", *range(256))
    fake_socket = FakeSocket()
    sleeps: list[float] = []

    next_sequence = sender.send_pcm_stream(
        "192.0.2.10",
        4242,
        io.BytesIO(pcm),
        initial_sequence=0xFFFF,
        socket_factory=lambda *_: fake_socket,
        clock=lambda: 0.0,
        sleep=sleeps.append,
    )

    assert next_sequence == 1
    assert [struct.unpack("<H", packet[6:8])[0] for packet, _ in fake_socket.packets] == [
        0xFFFF,
        0,
    ]
    assert all(address == ("192.0.2.10", 4242) for _, address in fake_socket.packets)
    assert sleeps == pytest.approx(
        [sender.PACKET_INTERVAL_SECONDS, 2 * sender.PACKET_INTERVAL_SECONDS]
    )


def test_send_pcm_stream_pads_partial_final_packet() -> None:
    fake_socket = FakeSocket()

    sender.send_pcm_stream(
        "192.0.2.10",
        4242,
        io.BytesIO(struct.pack("<h", -1234)),
        socket_factory=lambda *_: fake_socket,
        clock=lambda: 0.0,
        sleep=lambda _: None,
    )

    packet = fake_socket.packets[0][0]
    samples = struct.unpack("<128h", packet[10:])
    assert samples[0] == -1234
    assert samples[1:] == (0,) * 127


def test_live_capture_cleanup_on_keyboard_interrupt(monkeypatch: pytest.MonkeyPatch) -> None:
    class FakeCapture:
        stopped = False
        closed = False

        def stop_stream(self) -> None:
            self.stopped = True

        def close(self) -> None:
            self.closed = True

    class FakeManager:
        capture = FakeCapture()

        def __enter__(self) -> "FakeManager":
            return self

        def __exit__(self, *args: object) -> None:
            return None

        def get_default_wasapi_loopback(self) -> dict[str, object]:
            return {
                "name": "Speakers [Loopback]",
                "defaultSampleRate": 48_000.0,
                "maxInputChannels": 2,
                "index": 4,
            }

        def open(self, **kwargs: object) -> FakeCapture:
            assert kwargs["rate"] == 48_000
            assert kwargs["channels"] == 2
            return self.capture

    class FakeProcess:
        stdout = io.BytesIO()
        stdin = io.BytesIO()
        terminated = False

        def poll(self) -> None:
            return None

        def terminate(self) -> None:
            self.terminated = True

        def wait(self, timeout: int | None = None) -> int:
            return 0

    class FakeThread:
        started = False
        joined = False

        def __init__(self, **kwargs: object) -> None:
            assert kwargs["target"] is sender.feed_loopback

        def start(self) -> None:
            self.started = True

        def join(self, timeout: int | None = None) -> None:
            assert timeout == 2
            self.joined = True

    manager = FakeManager()
    process = FakeProcess()
    fake_pyaudio = SimpleNamespace(PyAudio=lambda: manager, paInt16=8)
    thread = FakeThread(target=sender.feed_loopback)

    monkeypatch.setattr(sender.importlib, "import_module", lambda _: fake_pyaudio)
    monkeypatch.setattr(sender, "start_live_ffmpeg", lambda *_: process)
    monkeypatch.setattr(sender.threading, "Thread", lambda **_: thread)
    monkeypatch.setattr(
        sender,
        "send_pcm_stream",
        lambda *_args, **_kwargs: (_ for _ in ()).throw(KeyboardInterrupt()),
    )

    with pytest.raises(KeyboardInterrupt):
        sender.stream_system_audio("192.0.2.10", 4242, "ffmpeg")

    assert thread.started and thread.joined
    assert manager.capture.stopped and manager.capture.closed
    assert process.terminated


def test_main_reports_missing_file(capsys: pytest.CaptureFixture[str]) -> None:
    return_code = sender.main(
        ["--host", "192.0.2.10", "--file", "missing-audio-file.mp3"]
    )

    assert return_code == 2
    assert "audio file not found" in capsys.readouterr().err
