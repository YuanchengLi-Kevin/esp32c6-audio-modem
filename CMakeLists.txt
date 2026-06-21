# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp32c6_audio_modem)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
  src/main.c
  src/features/uart_audio/uart_audio.c
)

target_sources_ifdef(CONFIG_AUDIO_MODEM_SOURCE_SINE app PRIVATE
  src/features/audio_source/sine_source.c
)

target_sources_ifdef(CONFIG_AUDIO_MODEM_SOURCE_UDP app PRIVATE
  src/features/jitter_buffer/jitter_buffer.c
  src/features/udp_audio/udp_audio.c
  src/features/wifi_station/wifi_station.c
)
