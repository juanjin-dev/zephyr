.. _rakwireless_rak14000:

RAK14000 WisBlock E-Ink Display Module
######################################

Overview
********

RAK14000 is a WisBlock module carrying a 2.13 inch electrophoretic display driven by a
Solomon Systech SSD1680 controller. It mounts on the I/O Slot of a WisBlock Base Board and
talks over SPI. Two panels are sold, and each has its own shield:

.. list-table::
   :header-rows: 1

   * - Shield
     - Panel
   * - ``rakwireless_rak14000_wb``
     - White and black, 212x104 pixels
   * - ``rakwireless_rak14000_wbr``
     - White, black and red, 250x122 pixels

A flexible cable carries three buttons, which the shield exposes as a ``gpio-keys``
node.

More information about the module can be found at `RAK14000 WisBlock E-Ink Display
Module`_.

Requirements
************

RAK14000 requires a WisBlock Base Board and a WisBlock Core module. The base board supplies
the I/O Slot nodes this shield attaches to, so its shield must be listed first.

Pin Assignments
***************

+-----------------+---------------------+
| Display signal  | WisBlock I/O Slot   |
+=================+=====================+
| CS              | SPI_CS              |
+-----------------+---------------------+
| SCK             | SPI_CLK             |
+-----------------+---------------------+
| SDIN            | SPI_MOSI            |
+-----------------+---------------------+
| D/C             | IO1                 |
+-----------------+---------------------+
| BUSY            | IO4                 |
+-----------------+---------------------+
| RESET           | RESET               |
+-----------------+---------------------+
| Button S1       | IO3                 |
+-----------------+---------------------+
| Button S2       | IO5                 |
+-----------------+---------------------+
| Button S3       | IO6                 |
+-----------------+---------------------+

The panel reset follows the WisBlock reset line rather than a GPIO, so the display comes
out of reset with the board and the driver resets the controller over SPI afterwards.

The three colour panel declares no partial refresh profile. Its red particles need a full
waveform, and the driver leaves the red plane off only for a panel that asks for no
partial refresh.

The module also wires SDIN to SPI_MISO through a second resistor, so a base board that
carries the display data on that line can be used by moving the fitted resistor.

Programming
***********

List the base board shield before this one so the I/O Slot nodes exist:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/display
   :board: rak4631/nrf52840
   :shield: rakwireless_rak19007,rakwireless_rak14000_wb
   :goals: build flash

References
**********

.. target-notes::

.. _RAK14000 WisBlock E-Ink Display Module:
   https://docs.rakwireless.com/product-categories/wisblock/rak14000
