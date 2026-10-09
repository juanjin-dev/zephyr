.. _rakwireless_rak14014:

RAK14014 WisBlock TFT LCD Display Module
########################################

Overview
********

RAK14014 is a WisBlock module carrying a 2.4 inch IPS TFT panel of 240x320 pixels driven
by a Sitronix ST7789 over SPI, a FocalTech FT6336U capacitive touch controller on I2C, and
a PCA9555 compatible I/O expander that reads up to six thin film buttons. It mounts on the
I/O Slot of a WisBlock Base Board.

The panel and the touch sensor are soldered to the module. The buttons arrive on two
flexible cables of three, and the expander pulls their inputs high, so a module used
without the cables reports no key presses.

More information about the module can be found at `RAK14014 WisBlock TFT LCD Display
Module`_.

Requirements
************

RAK14014 requires a WisBlock Base Board and a WisBlock Core module. The base board supplies
the I/O Slot nodes this shield attaches to, so its shield must be listed first.

Pin Assignments
***************

+---------------------+---------------------+
| Module signal       | WisBlock I/O Slot   |
+=====================+=====================+
| Display CS          | SPI_CS              |
+---------------------+---------------------+
| Display SCL         | SPI_CLK             |
+---------------------+---------------------+
| Display DATA        | SPI_MOSI            |
+---------------------+---------------------+
| Display RS          | IO4                 |
+---------------------+---------------------+
| Backlight           | IO3                 |
+---------------------+---------------------+
| Reset               | IO5                 |
+---------------------+---------------------+
| Interrupt           | IO6                 |
+---------------------+---------------------+
| Touch and expander  | I2C1                |
+---------------------+---------------------+

The panel carries no data output, so the display is driven write only.

One reset line serves both the panel and the touch controller, and the shield gives it to
the display, whose reset therefore also resets the touch sensor. One interrupt line serves
both the touch controller and the expander, which answer on it through open drain outputs,
so a falling edge means that either of them has something to report.

The backlight is a regulator the board switches through IO3 rather than a light the
application drives, so it comes up with the display.

Buttons
*******

The expander reads the buttons on its first six pins. Each is reported as a key event, and
an application that wants other key codes overrides them through the button labels:

.. list-table::
   :header-rows: 1

   * - Button
     - Expander pin
     - Key code
     - Label
   * - BUTTON1
     - 0
     - ``INPUT_KEY_0``
     - ``rak14014_button1``
   * - BUTTON2
     - 1
     - ``INPUT_KEY_1``
     - ``rak14014_button2``
   * - BUTTON3
     - 2
     - ``INPUT_KEY_2``
     - ``rak14014_button3``
   * - BUTTON4
     - 3
     - ``INPUT_KEY_3``
     - ``rak14014_button4``
   * - BUTTON5
     - 4
     - ``INPUT_KEY_4``
     - ``rak14014_button5``
   * - BUTTON6
     - 5
     - ``INPUT_KEY_5``
     - ``rak14014_button6``

Programming
***********

List the base board shield before this one so the I/O Slot nodes exist:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/display
   :board: rak4631/nrf52840
   :shield: rakwireless_rak19007,rakwireless_rak14014
   :goals: build flash

References
**********

.. target-notes::

.. _RAK14014 WisBlock TFT LCD Display Module:
   https://docs.rakwireless.com/product-categories/wisblock/rak14014
