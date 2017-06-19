==================================================
VME Scripts
==================================================

Overview
--------
VME Scripts are plain text files with one command per line. Comments may be started using the "#"
character. They extend to the end of the line.

Scripts belonging to a module (Module Init, VME Interface Settings, Module Reset and the readout
code) will have the module base address added to most of the commands. This allows writing scripts
containing module-relative addresses only. An exception is the :ref:`writeabs <vme_command_write>` command which does not
modify its address argument. The base address can also be temporarily replaced with a different
value by using the :ref:`vme_command_setbase` and :ref:`vme_comand_resetbase` commands.

The commands below use the following values for address modifiers and data widths:

.. _vme_address_modes:

+-----------------------+
| Address Modes (amode) |
+=======================+
| a16                   |
+-----------------------+
| a24                   |
+-----------------------+
| a32                   |
+-----------------------+

.. _vme_data_widths:

+----------------------+
| Data Widths (dwidth) |
+======================+
| d16                  |
+----------------------+
| d32                  |
+----------------------+

The combination of amode, dwidth and BLT/MBLT yields a VME address modifier to be sent over the bus.
Internally these non-privileged (aka user) address modifiers will be used:
* A16: 0x29
* A24: 0x39, BLT=0x3b
* A32: 0x09, BLT=0x0b, MBLT=0x08

Numbers in the script (addresses, transfer counts, masks) may be specified in decimal, octal or hex
using the standard C prefixes (0x for hex, 0 for octal). Additionally register values may be written
in binary starting with a prefix of 0b followed by 0s and 1s, optionally separated by ' characters.
Example: 0b1010'0101'1100'0011 is equal to 0xa5c3

.. _vme_commands:

Commands
--------

.. _vme_command_write:
.. _vme_command_writeabs:

Writing
~~~~~~~
* ``write <amode> <dwidth> <address> <value>``
* ``writeabs <amode> <dwidth> <address> <value>``

**writeabs** uses the given *<address>* unmodified, meaning the module base address will not be added.

There is a short syntax version of the write command: if a line consists of only two numbers
separated by whitespace, a write using 32-bit addressing (a32) and 16-bit register width (d16) is
assumed. The address is the first number, the value to be written is the second number.

Example: ``0xbb006070 3`` is the same as ``write a32 d16 0xbb006070 3``

.. _vme_command_read:

Reading
~~~~~~~
