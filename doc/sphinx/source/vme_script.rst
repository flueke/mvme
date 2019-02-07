.. _vme-script-reference:

==================================================
VME Scripts
==================================================

Overview
--------
VME Scripts are plain text files with one command per line. Comments may be
started using the ``#`` character. They extend to the end of the line.

Scripts belonging to a module (**Module Init**, **VME Interface Settings**,
**Module Reset** and the readout code) will have the **module base address**
added to most of the commands. This allows writing scripts containing
module-relative addresses only. An exception is the :ref:`writeabs
<vme-command-writeabs>` command which does not modify its address argument. The
base address can also be temporarily replaced with a different value by using
the :ref:`setbase <vme-command-setbase>` and :ref:`resetbase
<vme-command-resetbase>` commands.

The commands below use the following values for address modifiers and data widths:

.. table:: VME Address Modes
  :name: vme-address-modes

  +------------------------------+
  | **Address Mode** (*<amode>*) |
  +==============================+
  | a16                          |
  +------------------------------+
  | a24                          |
  +------------------------------+
  | a32                          |
  +------------------------------+

.. only:: html

   |

.. table:: VME Data Widths
  :name: vme-data-widths

  +-----------------------------+
  | **Data Width** (*<dwidth>*) |
  +=============================+
  | d16                         |
  +-----------------------------+
  | d32                         |
  +-----------------------------+

The combination of amode, dwidth and BLT/MBLT yields a VME address modifier to be sent over the bus.
Internally these non-privileged (aka user) address modifiers will be used:

.. table:: VME address modifiers used by mvme
  :name: vme-address-modifiers

  +-----------+------------+---------+----------+
  | **amode** | **single** | **BLT** | **MBLT** |
  +===========+============+=========+==========+
  | A16       | 0x29       |         |          |
  +-----------+------------+---------+----------+
  | A24       | 0x39       | 0x3b    |          |
  +-----------+------------+---------+----------+
  | A32       | 0x09       | 0x0b    | 0x08     |
  +-----------+------------+---------+----------+

Numbers in the script (addresses, transfer counts, masks) may be specified in decimal, octal or hex
using the standard C prefixes (``0x`` for hex, ``0`` for octal). Additionally register values may be
written in binary starting with a prefix of ``0b`` followed by ``0``\ s and ``1``\ s, optionally
separated by ``'`` characters.

Example: ``0b1010'0101'1100'0011`` is equal to ``0xa5c3``

.. _vme-script-commands:

Commands
--------

.. _vme-command-write:
.. _vme-command-writeabs:

Writing
~~~~~~~
* **write** *<amode> <dwidth> <address> <value>*
* **writeabs** *<amode> <dwidth> <address> <value>*

**writeabs** uses the given *<address>* unmodified, meaning the module base address will not be added.

There is a short syntax version of the write command: if a line consists of only two numbers
separated by whitespace, a write using 32-bit addressing (a32) and 16-bit register width (d16) is
assumed. The address is the first number, the value to be written is the second number.

Example: ``0x6070 3`` is the same as ``write a32 d16 0x6070 3``

.. _vme-command-read:

Reading
~~~~~~~
* **read** *<amode> <dwidth> <address>*

Reads a single value from the given *<address>*.

.. _vme-command-blt:
.. _vme-command-bltfifo:
.. _vme-command-mblt:
.. _vme-command-mbltfifo:

Block Transfers
~~~~~~~~~~~~~~~
mvme supports the following read-only block transfer commands:

* **blt** *<amode> <address> <count>*
* **bltfifo** *<amode> <address> <count>*
* **mblt** *<amode> <address> <count>*
* **mbltfifo** *<amode> <address> <count>*

**blt** and **bltfifo** transfer *<count>* number of 32-bit words, **mblt** and **mbltfifo**
transfer 64-bit words.

The **\*fifo** variants do not increment the given starting address.

.. _vme-command-bltcount:
.. _vme-command-bltfifocount:
.. _vme-command-mbltcount:
.. _vme-command-mbltfifocount:

Variable sized Block Transfers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
* **bltcount** *<reg_amode> <reg_dwidth> <reg_addr> <reg_count_mask> <block_amode> <block_addr>*
* **bltfifocount** *<reg_amode> <reg_dwidth> <reg_addr> <reg_count_mask> <block_amode> <block_addr>*
* **mbltcount** *<reg_amode> <reg_dwidth> <reg_addr> <reg_count_mask> <block_amode> <block_addr>*
* **mbltfifocount** *<reg_amode> <reg_dwidth> <reg_addr> <reg_count_mask> <block_amode> <block_addr>*

These commands read the number of transfers to perform from the register at *<reg_addr>*, using the
given *<reg_amode>* and *<reg_dwidth>* as access modifiers. The value read is then AND'ed with
*<reg_count_mask>*. The resulting value is the number of block transfers to perform, starting at
*<block_addr>*.


Miscellaneous
~~~~~~~~~~~~~
.. _vme-command-wait:

* **wait** *<waitspec>*

Delays script execution for the given amount of time. *<waitspec>* is a number followed by one of
``ns``, ``ms`` or ``s`` for nanoseconds, milliseconds and seconds respectively. If no suffix is
given milliseconds are assumed.

Note: When creating a command stack to be executed by the VMUSB Controller in DAQ Mode the
resolution of the waitspec is **200 ns** and the maximum value is **51000 ns**.

Example: ``wait 500ms # Delay script execution for 500ms``

.. _vme-command-marker:

* **marker** *<marker_word>*

The marker command adds a 32-bit marker word into the data stream. This can be used to separate data
from different modules.

.. _vme-command-setbase:
.. _vme-command-resetbase:

* **setbase** *<address>*
* **resetbase**

These commands can be used to temporarily replace the current base address with a different value.
**setbase** sets a new base address, which will be effective for all following commands. Use
**resetbase** to restore the original base address.

VMUSB specific
~~~~~~~~~~~~~~
.. _vme_command-vmusb-write-reg
* **vmusb_write_reg** *(<register_address>|<register_name>) <value>*
* **vmusb_read_reg** *(<register_address>|<register_name>)*

These commands only work when using the WIENER VM-USB controller and allow
read/write access to its internal registers. For details on the registers see
the VM-USB manual section *3.4 - Internal Register File*.

Instead of using register addresses some registers are also accessible via
name. The following name mappings are defined:

.. table:: VMUSB Register Names
  :name: vmusb-register-names

  +-------------------+-------------+
  | **Register Name** | **address** |
  +===================+=============+
  | dev_src           | 0x10        |
  +-------------------+-------------+
  | dgg_a             | 0x14        |
  +-------------------+-------------+
  | dgg_b             | 0x18        |
  +-------------------+-------------+
  | dgg_ext           | 0x38        |
  +-------------------+-------------+
  | sclr_a            | 0x1c        |
  +-------------------+-------------+
  | sclr_b            | 0x20        |
  +-------------------+-------------+
  | daq_settings      | 0x08        |
  +-------------------+-------------+

Example
-------
::

    # BLT readout until BERR or number of transfers reached
    bltfifo a32 0x0000 10000

    # Write the value 3 to address 0x6070. If this appears in a module specific
    # script (init, readout, reset) the module base address is added to the
    # given address.
    0x6070 3

    # Same as above but explicitly using the write command.
    write a32 d16 0x6070 3

    # Set a different base address. This will replace the current base address
    # until resetbase is used.
    setbase 0xbb000000

    # Results in an a32/d16 write to 0xbb006070.
    0x6070 5

    # Restore the original base address.
    resetbase

    # Binary notation for the register value.
    0x6070 0b0000'0101
