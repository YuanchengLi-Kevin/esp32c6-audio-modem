.. Copyright (c) 2026 Yuancheng Li
.. SPDX-License-Identifier: Apache-2.0

Python UART audio tests
#######################

Install the test dependencies:

.. code-block:: console

   python -m pip install -r tests/python/requirements.txt

Run the protocol decoder tests without hardware:

.. code-block:: console

   python -m pytest tests/python -m "not hardware"

To include hardware tests, connect a 3.3 V USB-to-UART adapter RX input to
ESP32-C6 GPIO4, connect ground, and select its serial port:

.. code-block:: powershell

   $env:AUDIO_UART_PORT = "COM7"
   python -m pytest tests/python

The hardware tests expect the board to already run firmware that emits the
documented 42 kHz, 128-sample UART stream at 2,000,000 baud. They do not build
or flash firmware.
