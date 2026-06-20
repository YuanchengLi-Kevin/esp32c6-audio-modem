.. Copyright (c) 2026 Yuancheng Li
.. SPDX-License-Identifier: Apache-2.0

ESP32-C6 Audio Modem
####################

Zephyr RTOS application for an ESP32-C6 used as a WiFi/audio coprocessor for a
TI MSPM0G3507 LaunchPad.

Overview
********

The ESP32-C6 streams unsigned 12-bit PCM samples to the MSPM0 over UART. The
initial firmware milestone generates a local sine wave so the MSP UART receiver
and DAC path can be tested before WiFi audio is added.

UART interfaces
***************

The application uses two independent interfaces:

* ``UART1`` carries only framed audio to the MSPM0 at 2,000,000 baud. Its TX
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

* 2,000,000 baud
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
1.30 ms to transmit at 2 Mbaud.

Sample format:

* 42,000 samples/sec
* Unsigned 12-bit sample carried in ``uint16_t``
* Practical range: 0..4095
* Silence/midpoint: 2048
* Maximum frame size: 128 samples

The current test source continuously generates a 656 Hz sine wave at 42,000
samples/second. It produces approximately 512..3584 around midpoint 2048. Each
128-sample frame represents approximately 3.048 ms of audio.

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
       uart_audio/
         uart_audio.h
         uart_audio.c

``src/features/audio_source`` owns sample generation. Replace this interface
with a WiFi-backed source later without changing the UART framing code.

``src/features/uart_audio`` owns framing and DMA transmission.

Building and Running
********************

.. code-block:: console

   west blobs fetch hal_espressif
   west build -b esp32c6_devkitc/esp32c6/hpcore .
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

Later WiFi/UDP Architecture
***************************

The second milestone should add a separate UDP-backed audio source while keeping
the UART frame transmitter unchanged:

.. code-block:: text

   WiFi STA connection
     -> UDP receive thread
     -> PCM packet parser / sample converter
     -> jitter buffer
     -> UART audio transmitter

Recommended source modules:

.. code-block:: text

   src/features/wifi_station/
   src/features/udp_audio/
   src/features/jitter_buffer/

WiFi station configuration should use Kconfig settings for SSID/PSK rather than
hard-coded credentials. A later ``prj.conf`` will need Zephyr networking, IPv4,
sockets, WiFi management, and Espressif WiFi support enabled.

For signed 16-bit PCM input, convert to the MSP format with:

.. code-block:: c

   sample12 = CLAMP((pcm16 + 32768) >> 4, 0, 4095);

On jitter-buffer underrun, output midpoint samples with value ``2048``.

ESP32-C6 WiFi Notes
*******************

Zephyr's ESP32-C6 DevKitC board target is
``esp32c6_devkitc/esp32c6/hpcore``. Espressif RF binary blobs are required for
WiFi operation:

.. code-block:: console

   west blobs fetch hal_espressif

Run that after ``west update``. WiFi support depends on Zephyr's Espressif HAL
module and the board/SoC support present in the Zephyr version in use.
