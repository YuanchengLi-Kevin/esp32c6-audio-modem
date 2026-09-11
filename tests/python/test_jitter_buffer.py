# Copyright (c) 2026 Yuancheng Li
# SPDX-License-Identifier: Apache-2.0

"""Compile the real jitter buffer with host-only, single-threaded kernel stubs."""

import shutil
import subprocess
from pathlib import Path

import pytest


HEADER = """/* Copyright (c) 2026 Yuancheng Li
 * SPDX-License-Identifier: Apache-2.0 */
"""


def test_jitter_buffer_recovery(tmp_path: Path) -> None:
    compiler = shutil.which("gcc")
    if compiler is None:
        pytest.skip("gcc is required for the C jitter buffer regression test")

    zephyr = tmp_path / "zephyr"
    (zephyr / "sys").mkdir(parents=True)
    (zephyr / "kernel.h").write_text(
        HEADER + """
#include <stdint.h>
#include <errno.h>
#ifndef ESTALE
#define ESTALE 116
#endif
struct k_mutex { int unused; };
#define K_FOREVER 0
static inline void k_mutex_init(struct k_mutex *m) { (void)m; }
static inline void k_mutex_lock(struct k_mutex *m, int t) { (void)m; (void)t; }
static inline void k_mutex_unlock(struct k_mutex *m) { (void)m; }
"""
    )
    (zephyr / "sys" / "util.h").write_text(
        HEADER + """
#define BUILD_ASSERT(condition, message) _Static_assert(condition, message)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
"""
    )
    harness = tmp_path / "jitter_buffer_test.c"
    harness.write_text(
        HEADER + """
#include <assert.h>
#include <errno.h>
#include "jitter_buffer.h"

static struct jitter_buffer buffer;
static uint16_t input[128];
static uint16_t output[128];

static void push(uint16_t sequence)
{
    for (int i = 0; i < 128; i++) { input[i] = sequence; }
    assert(jitter_buffer_push(&buffer, sequence, input, 128) == 0);
}

static void pop(uint16_t sequence)
{
    assert(jitter_buffer_pop(&buffer, output, 128));
    for (int i = 0; i < 128; i++) { assert(output[i] == sequence); }
}

static void silence(void)
{
    assert(!jitter_buffer_pop(&buffer, output, 128));
    for (int i = 0; i < 128; i++) { assert(output[i] == 2048); }
}

int main(void)
{
    /* Recover after playback underflows and a partial refill is stranded. */
    jitter_buffer_init(&buffer);
    push(0); push(1); push(2);
    pop(0); pop(1); pop(2);
    silence();
    push(4);
    silence();
    push(12); push(13); push(14);
    pop(12); pop(13); pop(14);
    assert(buffer.packet_count == 0);

    /* Startup recovery also clears every old slot, including two packets. */
    jitter_buffer_init(&buffer);
    push(0); push(1);
    silence();
    push(8); push(9); push(10);
    pop(8); pop(9); pop(10);
    assert(buffer.packet_count == 0);

    /* A forward gap across uint16 rollover can recover during refill. */
    jitter_buffer_init(&buffer);
    push(65532);
    push(4); push(5); push(6);
    pop(4); pop(5); pop(6);

    /* Keep active playback, reordering, duplicate and stale handling intact. */
    jitter_buffer_init(&buffer);
    push(65534); push(0); push(65535);
    pop(65534);
    assert(jitter_buffer_push(&buffer, 7, input, 128) == -ENOBUFS);
    assert(jitter_buffer_push(&buffer, 65534, input, 128) == -ESTALE);
    assert(jitter_buffer_push(&buffer, 0, input, 128) == -EALREADY);
    pop(65535); pop(0);
    return 0;
}
"""
    )
    source_dir = Path(__file__).resolve().parents[2] / "src/features/jitter_buffer"
    executable = tmp_path / "jitter_buffer_test.exe"
    subprocess.run(
        [
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-DCONFIG_AUDIO_MODEM_JITTER_BUFFER_PACKETS=8",
            "-DCONFIG_AUDIO_MODEM_JITTER_BUFFER_START_PACKETS=3",
            "-I", str(tmp_path), "-I", str(source_dir),
            str(harness), str(source_dir / "jitter_buffer.c"),
            "-o", str(executable),
        ],
        check=True, capture_output=True, text=True,
    )
    subprocess.run([str(executable)], check=True, capture_output=True, text=True)
