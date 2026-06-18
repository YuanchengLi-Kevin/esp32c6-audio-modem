ESP32-C6 Audio Modem
####################

Zephyr RTOS application for an ESP32-C6 used as a WiFi/audio coprocessor for a
TI MSPM0G3507 LaunchPad.

Overview
********

The ESP32-C6 streams unsigned 12-bit PCM samples to the MSPM0 over UART. The
initial firmware milestone generates a local sine wave so the MSP UART receiver
and DAC path can be tested before WiFi audio is added.

MSP-side protocol
*****************

UART settings:

* Baud: 2,000,000
* Logic level: 3.3 V
* ESP32-C6 TX -> MSPM0 PA18 / UART1 RX
* ESP32-C6 RX <- MSPM0 PA17 / UART1 TX, optional future control/status path
* Shared GND required

Frame format:

.. code-block:: text

   byte 0: 0xA5
   byte 1: 0x5A
   byte 2: sequence number, uint8_t
   byte 3: sample count, uint8_t, 1..128
   bytes 4+: PCM samples, little-endian uint16_t

Sample format:

* 42,000 samples/sec
* Unsigned 12-bit sample carried in ``uint16_t``
* Practical range: 0..4095
* Silence/midpoint: 2048
* Maximum frame size: 128 samples

Project layout
**************

.. code-block:: text

   CMakeLists.txt
   prj.conf
   boards/
     esp32c6_devkitc.overlay
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

``src/features/uart_audio`` owns the MSP UART frame protocol.

ESP32-C6 UART pins
******************

The board overlay enables ``uart1`` at 2 Mbaud and selects:

* GPIO4 as ``UART1_TX`` to MSPM0 PA18
* GPIO5 as ``UART1_RX`` from MSPM0 PA17, optional for later control/status

GPIO16/GPIO17 are left for the ESP32-C6 DevKitC USB-to-UART bridge console.
GPIO12/GPIO13 are avoided because they are native USB D-/D+ pins on the
DevKitC header.

Building and Running
********************

Do not build or flash from automation in this repository unless explicitly
requested. Manual commands:

.. code-block:: console

   west blobs fetch hal_espressif
   west build -b esp32c6_devkitc/esp32c6/hpcore .
   west flash
   west espressif monitor

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
