.. _vme-script-reference:

.. TODO: difference between uploading script to the controller and running them.
.. TODO: where do reads go? how is waiting handled. MVLC does not support waiting!

==================================================
VME Scripts
==================================================

Overview
--------
VME Scripts are plain text files with one command per line. Comments may be
started using the ``#`` character. They extend to the end of the line.
Alternatively blocks can be commented out starting with ``/*`` and ending with
``*/``.

Scripts belonging to a module (**Module Init Scripts**, **VME Interface
Settings**, **Module Reset** and the readout code) will have the **module base
address** added to most of the commands. This allows writing scripts containing
module-relative addresses only. An exception is the :ref:`writeabs
<vme-command-writeabs>` command which does not modify its address argument. The
base address can also be temporarily replaced with a different value by using
the :ref:`setbase <vme-command-setbase>` and :ref:`resetbase
<vme-command-resetbase>` commands.

The commands below accept the following values for address modifiers and data widths:

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

Miscellaneous
~~~~~~~~~~~~~

.. _vme-command-wait:
wait
^^^^
* **wait** *<waitspec>*

Delays script execution for the given amount of time. *<waitspec>* is a number followed by one of
``ns``, ``ms`` or ``s`` for nanoseconds, milliseconds and seconds respectively. If no suffix is
given milliseconds are assumed.

.. note::
  The wait command is only available when directly executing a script from
  within mvme. It is not supported in command stacks for the MVLC and SIS3153
  controllers.

  The VMUSB has limited support for the wait command in command stacks with a
  waitspec resolution of **200 ns** and the maximum possible delay being
  **51000 ns**.

Example: ``wait 500ms # Delay script execution for 500ms``

.. _vme-command-marker:
marker
^^^^^^

* **marker** *<marker_word>*

The marker command adds a 32-bit marker word into the data stream. This can be used to separate data
from different modules.

.. _vme-command-setbase:
.. _vme-command-resetbase:
setbase/resetbase
^^^^^^^^^^^^^^^^^

* **setbase** *<address>*
* **resetbase**

These commands can be used to temporarily replace the current base address with a different value.
**setbase** sets a new base address, which will be effective for all following commands. Use
**resetbase** to restore the original base address.

.. _vme-command-write-float-word:
write_float_word
^^^^^^^^^^^^^^^^

* **write_float_word** *<address_mode>* *<address>* *<part>* *<value>*

The write_float_word command is a helper function for dealing with VME modules
using IEEE-754 floating point numbers internally (e.g. the ISEG VHS4030). The
command writes a 16-bit part of a 32-bit float into the given register without
performing any integer conversions.

Arguments:

* *address_mode*

  The VME address mode: a16, a24 or a32

* *address*

  Address of the register to write to.

* *part*

  One of **upper** / **1** and  **lower** / **0**. The upper part contains the
  16 most significant bits of the float, the lower part the 16 least
  significant bits.

* *value*

  The floating point value using a *.* as the decimal separator.

Example:::

  write_float_word a16 0x0014 upper 3.14
  write_float_word a16 0x0016 lower 3.14

Writes the 32-bit float value *3.14* to the two 16-bit registers 0x14 and 0x16.

VMUSB specific
~~~~~~~~~~~~~~
.. _vme_command-vmusb-write-reg:

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

Floating Point Values, Variables and Mathematical Expressions
-------------------------------------------------------------
Since mvme-0.9.7 VME scripts support evaluation of numerical expressions and
can contain references to variables. Additionally floating point values can be
used where previously only unsigned integers where allowed.

It is up to each specific command how floating point values are interpreted and
what limits are imposed. The VME read and write commands use mathematical
rounding and test that the resulting value fits in an unsigned 16 or 32 bit
integer (depending on the commands data width argument). On the other hand the
`vme-command-write-float-word` command uses the floating point value directly
without doing any conversion to integer.

Variables
~~~~~~~~~


Expressions
~~~~~~~~~~~

.. _exprtk: http://www.partow.net/programming/exprtk/index.html

A mathematical expression in VME scripts is enclosed between ``$(`` and ``)``.
The enclosed string (including the outermost parentheses) is passed to the
`exprtk`_ library for evaluation and the resulting value replaces the
expression string before further parsing is done.

exprtk internally uses floating point arithmetic and the result of evaluating
an expression is always a floating point value.

exprtk internally uses floating point arithmetic but most of the values inside
VME scripts are unsigned 16 or 32 bit values. For this reason the result of the
expression evaluation is rounded to the nearest integer value and converted to
a 32-bit unsigned. If the evaluation yields a negative number the parser yields
an error and stops further processing of the script.



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
