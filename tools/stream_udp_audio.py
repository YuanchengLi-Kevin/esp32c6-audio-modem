# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

"""Stream file or Windows system audio to the ESP32-C6 over UDP."""

import argparse
import importlib
import queue
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from collections.abc import Callable
from pathlib import Path
from typing import BinaryIO


SAMPLE_RATE_HZ = 10_000
SAMPLES_PER_PACKET = 128
BYTES_PER_SAMPLE = 2
PCM_PACKET_BYTES = SAMPLES_PER_PACKET * BYTES_PER_SAMPLE
PACKET_INTERVAL_SECONDS = SAMPLES_PER_PACKET / SAMPLE_RATE_HZ
CAPTURE_FRAMES = 1024


def add_repo_imports() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(repo_root / "tests" / "python"))


add_repo_imports()
from udp_audio import encode_udp_audio  # noqa: E402


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stream an audio file or Windows system audio to the ESP32-C6."
    )
    parser.add_argument("--host", required=True, help="ESP32 IPv4 address")
    parser.add_argument("--port", type=int, default=4242, help="ESP32 UDP audio port")

    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--file", type=Path, help="Audio file to stream")
    source.add_argument(
        "--system-audio",
        action="store_true",
        help="Capture the default Windows playback device with WASAPI loopback",
    )

    parser.add_argument(
        "--ffmpeg",
        default="ffmpeg",
        help="ffmpeg executable path, defaults to ffmpeg on PATH",
    )
    return parser.parse_args(argv)


def require_ffmpeg(ffmpeg: str) -> None:
    if shutil.which(ffmpeg) is None and not Path(ffmpeg).exists():
        raise FileNotFoundError(f"ffmpeg executable not found: {ffmpeg}")


def start_file_ffmpeg(ffmpeg: str, audio_file: Path) -> subprocess.Popen[bytes]:
    require_ffmpeg(ffmpeg)
    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(audio_file),
        "-f",
        "s16le",
        "-ac",
        "1",
        "-ar",
        str(SAMPLE_RATE_HZ),
        "pipe:1",
    ]
    return subprocess.Popen(command, stdout=subprocess.PIPE)


def start_live_ffmpeg(
    ffmpeg: str, input_rate_hz: int, input_channels: int
) -> subprocess.Popen[bytes]:
    require_ffmpeg(ffmpeg)
    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-f",
        "s16le",
        "-ac",
        str(input_channels),
        "-ar",
        str(input_rate_hz),
        "-i",
        "pipe:0",
        "-f",
        "s16le",
        "-ac",
        "1",
        "-ar",
        str(SAMPLE_RATE_HZ),
        "pipe:1",
    ]
    return subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)


def read_pcm_packet(stream: BinaryIO) -> bytes | None:
    frame = bytearray()
    while len(frame) < PCM_PACKET_BYTES:
        chunk = stream.read(PCM_PACKET_BYTES - len(frame))
        if not chunk:
            break
        frame.extend(chunk)

    if not frame:
        return None
    frame.extend(b"\x00" * (PCM_PACKET_BYTES - len(frame)))
    return bytes(frame)


def send_pcm_stream(
    host: str,
    port: int,
    pcm_stream: BinaryIO,
    *,
    initial_sequence: int = 0,
    socket_factory: Callable[..., socket.socket] = socket.socket,
    clock: Callable[[], float] = time.perf_counter,
    sleep: Callable[[float], None] = time.sleep,
) -> int:
    sequence = initial_sequence
    next_send_time = clock()

    with socket_factory(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        while True:
            frame = read_pcm_packet(pcm_stream)
            if frame is None:
                break

            samples = struct.unpack(f"<{SAMPLES_PER_PACKET}h", frame)
            sock.sendto(encode_udp_audio(sequence, samples), (host, port))
            sequence = (sequence + 1) & 0xFFFF

            next_send_time += PACKET_INTERVAL_SECONDS
            sleep_time = next_send_time - clock()
            if sleep_time > 0:
                sleep(sleep_time)

    return sequence


def stop_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return

    process.terminate()
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def stream_file(host: str, port: int, audio_file: Path, ffmpeg: str) -> None:
    process = start_file_ffmpeg(ffmpeg, audio_file)
    if process.stdout is None:
        stop_process(process)
        raise RuntimeError("ffmpeg stdout was not captured")

    try:
        send_pcm_stream(host, port, process.stdout)
        return_code = process.wait()
    except BaseException:
        stop_process(process)
        raise

    if return_code != 0:
        raise RuntimeError(f"ffmpeg exited with status {return_code}")


def feed_loopback(
    capture_stream: object,
    process: subprocess.Popen[bytes],
    stop_event: threading.Event,
    errors: queue.SimpleQueue[BaseException],
) -> None:
    if process.stdin is None:
        errors.put(RuntimeError("ffmpeg stdin was not captured"))
        return

    try:
        while not stop_event.is_set():
            pcm = capture_stream.read(CAPTURE_FRAMES, exception_on_overflow=False)
            if pcm:
                process.stdin.write(pcm)
    except (OSError, RuntimeError, BrokenPipeError) as exc:
        if not stop_event.is_set():
            errors.put(exc)
    finally:
        try:
            process.stdin.close()
        except OSError:
            pass


def stream_system_audio(host: str, port: int, ffmpeg: str) -> None:
    try:
        pyaudio = importlib.import_module("pyaudiowpatch")
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "PyAudioWPatch is required for --system-audio; "
            "install tools/requirements.txt on Windows"
        ) from exc

    stop_event = threading.Event()
    errors: queue.SimpleQueue[BaseException] = queue.SimpleQueue()

    with pyaudio.PyAudio() as manager:
        try:
            device = manager.get_default_wasapi_loopback()
        except OSError as exc:
            raise RuntimeError("default WASAPI loopback device was not found") from exc

        input_rate_hz = int(device["defaultSampleRate"])
        input_channels = int(device["maxInputChannels"])
        if input_rate_hz <= 0 or input_channels <= 0:
            raise RuntimeError("default WASAPI loopback device has an invalid format")

        print(
            f"Capturing system audio from {device['name']}: "
            f"{input_rate_hz} Hz, {input_channels} channel(s)"
        )
        process = start_live_ffmpeg(ffmpeg, input_rate_hz, input_channels)
        if process.stdout is None:
            stop_process(process)
            raise RuntimeError("ffmpeg stdout was not captured")

        capture_stream = None
        feeder = None
        return_code = None
        try:
            capture_stream = manager.open(
                format=pyaudio.paInt16,
                channels=input_channels,
                rate=input_rate_hz,
                frames_per_buffer=CAPTURE_FRAMES,
                input=True,
                input_device_index=int(device["index"]),
            )
            feeder = threading.Thread(
                target=feed_loopback,
                args=(capture_stream, process, stop_event, errors),
                name="wasapi_loopback",
                daemon=True,
            )
            feeder.start()

            send_pcm_stream(host, port, process.stdout)
            return_code = process.poll()
        finally:
            stop_event.set()
            try:
                if capture_stream is not None:
                    capture_stream.stop_stream()
            finally:
                stop_process(process)
                if feeder is not None:
                    feeder.join(timeout=2)
                if capture_stream is not None:
                    capture_stream.close()

    if not errors.empty():
        raise RuntimeError(f"system audio capture failed: {errors.get()}")
    if return_code != 0:
        raise RuntimeError(f"ffmpeg exited with status {return_code}")


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.file is not None and not args.file.exists():
        print(f"audio file not found: {args.file}", file=sys.stderr)
        return 2

    try:
        if args.system_audio:
            stream_system_audio(args.host, args.port, args.ffmpeg)
        else:
            stream_file(args.host, args.port, args.file, args.ffmpeg)
    except KeyboardInterrupt:
        print("Audio streaming stopped.")
        return 130
    except (OSError, RuntimeError) as exc:
        print(exc, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
