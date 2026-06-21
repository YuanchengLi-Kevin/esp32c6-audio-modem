.. Copyright (c) 2026 Yuancheng Li
.. SPDX-License-Identifier: Apache-2.0

ESP32-C6 Audio Modem
####################

Zephyr RTOS application for an ESP32-C6 used as a Wi-Fi/audio coprocessor for a
TI MSPM0G3507 LaunchPad.

Overview
********

The ESP32-C6 receives signed 16-bit PCM audio over Wi-Fi/UDP, absorbs network
jitter in a packet buffer, converts samples to unsigned 12-bit PCM, and streams
them to the MSPM0 over UART.

UART interfaces
***************

The application uses two independent interfaces:

* ``UART1`` carries only framed audio to the MSPM0 at 1,000,000 baud. Its TX
  signal is routed to ESP32-C6 GPIO4.
* ``usb_serial`` is the Zephyr console used by ``printk()``. It appears as the
  ESP32-C6 native USB Serial/JTAG COM port and is not connected to the audio
  stream. A serial monitor may select 115,200 baud; USB Serial/JTAG does not use
  that setting to clock physical UART pins.

UART audio wiring
*****************

Connect the boards while they are powered off:

.. code-block:: text

   ESP32-C6 GPIO4 / UART1 TX  ->  MSPM0 PA9 / UART1 RX
   ESP32-C6 GND               ->  MSPM0 GND

On the MSPM0G3507 LaunchPad, configure jumper J14 to connect PA9 for UART1 RX.
Only the ESP32-C6 TX connection is required; leave the MSPM0 TX and ESP32-C6
GPIO5 disconnected. Both boards use 3.3 V UART logic.

A shared ground is mandatory. Do not connect the boards' 3V3 power rails when
both boards are independently powered. Connect 3V3 only if one board is
intentionally powering the other and its regulator has sufficient capacity.

UART audio configuration
========================

The dedicated audio UART uses:

* 1,000,000 baud
* 8 data bits
* No parity
* 1 stop bit
* No hardware flow control

Frame format:

.. code-block:: text

   byte 0: 0xA5
   byte 1: 0x5A
   byte 2: sequence number, uint8_t
   byte 3: sample count, uint8_t, 1..128
   bytes 4+: PCM samples, little-endian uint16_t

Normal frames always contain 128 samples and therefore appear as:

.. code-block:: text

   A5 5A <sequence> 80 <256 sample bytes>

The sequence byte increments for each accepted DMA transmission and wraps
naturally from 255 to 0. A normal frame is 260 bytes and takes approximately
2.60 ms to transmit at 1 Mbaud.

Sample format:

* 42,000 samples/sec
* Unsigned 12-bit sample carried in ``uint16_t``
* Practical range: 0..4095
* Silence/midpoint: 2048
* Maximum frame size: 128 samples

Each 128-sample UART frame represents approximately 3.048 ms of audio. The
stream outputs midpoint samples while the jitter buffer is starting or empty.

UDP audio protocol
******************

The ESP32-C6 listens on the configured IPv4 UDP port. Each datagram contains
one independently validated audio packet:

.. code-block:: text

   bytes 0..3:  ASCII magic "AUD0"
   byte 4:      protocol version, currently 1
   byte 5:      flags, must be 0
   bytes 6..7:  packet sequence, little-endian uint16_t
   bytes 8..9:  sample count, little-endian uint16_t, must be 128
   bytes 10+:   signed 16-bit PCM samples, little-endian

The datagram length must be exactly 266 bytes. Sequence numbers wrap from 65535
to 0. Duplicate, stale, malformed, and packets outside the jitter-buffer window
are discarded. Fixed 128-sample packets ensure that replacing a missing packet
with silence preserves the stream timeline.

Signed PCM is converted to the MSP format with:

.. code-block:: c

   sample12 = (pcm16 + 32768) >> 4;

The jitter buffer accepts limited packet reordering. It begins playback after
``CONFIG_AUDIO_MODEM_JITTER_BUFFER_START_PACKETS`` packets and substitutes
midpoint value ``2048`` for unavailable audio.

DMA transmission
================

``src/features/uart_audio`` serializes each frame into one reusable 260-byte
buffer and submits it through Zephyr's asynchronous UART DMA API. The buffer is
not modified until ``UART_TX_DONE`` or ``UART_TX_ABORTED`` releases it. UART1 is
dedicated to binary audio, so console text is never written to GPIO4.

Project layout
**************

.. code-block:: text

   CMakeLists.txt
   prj.conf
   boards/
     esp32c6_devkitc_esp32c6_hpcore.overlay
   src/
     main.c
     features/
       audio_source/
         audio_source.h
         sine_source.c
       jitter_buffer/
         jitter_buffer.h
         jitter_buffer.c
       udp_audio/
         udp_audio.h
         udp_audio.c
       uart_audio/
         uart_audio.h
         uart_audio.c
       wifi_station/
         wifi_station.h
         wifi_station.c

``wifi_station`` owns station association and connection events. ``udp_audio``
owns the socket, packet validation, and PCM conversion. ``jitter_buffer`` owns
the synchronized producer/consumer boundary. ``uart_audio`` owns framing and
DMA transmission.

Audio source selection
**********************

Wi-Fi UDP audio is the default source. The local 656 Hz sine-wave source is
retained for UART, MSPM0 receiver, and DAC-path testing. Select it with the
provided configuration fragment:

.. code-block:: console

   west build -b esp32c6_devkitc/esp32c6/hpcore . -- \
     -DEXTRA_CONF_FILE=sine.conf

``CONFIG_AUDIO_MODEM_SOURCE_UDP`` and ``CONFIG_AUDIO_MODEM_SOURCE_SINE`` form a
Kconfig choice, so only the selected source modules are compiled into the
application.

Wi-Fi credentials
******************

Credentials are Kconfig values but should not be committed. Copy the provided
example and edit the ignored local file:

.. code-block:: console

   cp wifi.conf.example wifi.conf

Set ``CONFIG_AUDIO_MODEM_WIFI_SSID`` and ``CONFIG_AUDIO_MODEM_WIFI_PSK`` in
``wifi.conf``. An empty PSK selects an open network. If the SSID remains empty,
the firmware continues sending silence over UART but does not connect.

Building and Running
********************

.. code-block:: console

   west blobs fetch hal_espressif
   west build -b esp32c6_devkitc/esp32c6/hpcore . -- -DEXTRA_CONF_FILE=wifi.conf
   west flash
   west espressif monitor

Open the native USB Serial/JTAG monitor before resetting the ESP32-C6 if the
single startup message is required. The audio stream begins immediately after
initialization and continues without a handshake from the MSPM0.

For a predictable startup sequence:

#. Connect GPIO4, PA9, and the common ground.
#. Flash and start the MSPM0 receiver.
#. Flash or reset the ESP32-C6 transmitter.
#. Confirm continuous framed data on GPIO4 or observe the MSPM0 output.

If the ESP32-C6 remains in download mode after flashing over USB Serial/JTAG:

.. code-block:: console

   west flash --runner esp32
   west flash --reset-type watchdog-reset

Wi-Fi/UDP architecture
**********************

Network reception and UART playback run as separate producer and consumer
paths:

.. code-block:: text

   Wi-Fi STA connection
     -> UDP receive thread
     -> PCM packet parser / sample converter
     -> jitter buffer
     -> UART audio transmitter

ESP32-C6 Wi-Fi Notes
********************

Zephyr's ESP32-C6 DevKitC board target is
``esp32c6_devkitc/esp32c6/hpcore``. Espressif RF binary blobs are required for
Wi-Fi operation:

.. code-block:: console

   west blobs fetch hal_espressif

Run that after ``west update``. Wi-Fi support depends on Zephyr's Espressif HAL
module and the board/SoC support present in the Zephyr version in use.
