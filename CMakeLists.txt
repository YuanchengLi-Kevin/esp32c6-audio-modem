# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp32c6_audio_modem)

target_sources(app PRIVATE
  src/main.c
  src/features/audio_source/sine_source.c
  src/features/uart_audio/uart_audio.c
)
