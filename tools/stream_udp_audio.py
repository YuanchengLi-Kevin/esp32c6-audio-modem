# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

"""Stream an audio file to the ESP32-C6 audio modem over UDP."""

import argparse
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path


SAMPLE_RATE_HZ = 42_000
SAMPLES_PER_PACKET = 128
BYTES_PER_SAMPLE = 2
PACKET_INTERVAL_SECONDS = SAMPLES_PER_PACKET / SAMPLE_RATE_HZ


def add_repo_imports() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(repo_root / "tests" / "python"))


add_repo_imports()
from udp_audio import encode_udp_audio  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stream a local audio file to the ESP32-C6 UDP audio receiver."
    )
    parser.add_argument("--host", required=True, help="ESP32 IPv4 address")
    parser.add_argument("--port", type=int, default=4242, help="ESP32 UDP audio port")
    parser.add_argument("--file", required=True, type=Path, help="Audio file to stream")
    parser.add_argument(
        "--ffmpeg",
        default="ffmpeg",
        help="ffmpeg executable path, defaults to ffmpeg on PATH",
    )
    return parser.parse_args()


def start_ffmpeg(ffmpeg: str, audio_file: Path) -> subprocess.Popen[bytes]:
    if shutil.which(ffmpeg) is None and not Path(ffmpeg).exists():
        raise FileNotFoundError(f"ffmpeg executable not found: {ffmpeg}")

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
        "-",
    ]
    return subprocess.Popen(command, stdout=subprocess.PIPE)


def read_packet_samples(stream: subprocess.Popen[bytes]) -> list[int] | None:
    if stream.stdout is None:
        raise RuntimeError("ffmpeg stdout was not captured")

    frame_size = SAMPLES_PER_PACKET * BYTES_PER_SAMPLE
    frame = stream.stdout.read(frame_size)
    if not frame:
        return None
    if len(frame) < frame_size:
        frame += b"\x00" * (frame_size - len(frame))

    return list(struct.unpack(f"<{SAMPLES_PER_PACKET}h", frame))


def stream_audio(host: str, port: int, audio_file: Path, ffmpeg: str) -> None:
    process = start_ffmpeg(ffmpeg, audio_file)
    sequence = 0
    next_send_time = time.perf_counter()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        while True:
            samples = read_packet_samples(process)
            if samples is None:
                break

            packet = encode_udp_audio(sequence, samples)
            sock.sendto(packet, (host, port))
            sequence = (sequence + 1) & 0xFFFF

            next_send_time += PACKET_INTERVAL_SECONDS
            sleep_time = next_send_time - time.perf_counter()
            if sleep_time > 0:
                time.sleep(sleep_time)

    return_code = process.wait()
    if return_code != 0:
        raise RuntimeError(f"ffmpeg exited with status {return_code}")


def main() -> int:
    args = parse_args()
    if not args.file.exists():
        print(f"audio file not found: {args.file}", file=sys.stderr)
        return 2

    try:
        stream_audio(args.host, args.port, args.file, args.ffmpeg)
    except (OSError, RuntimeError) as exc:
        print(exc, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
